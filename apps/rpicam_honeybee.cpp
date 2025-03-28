/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * rpicam_still.cpp - libcamera stills capture app.
 */
#include <chrono>
#include <filesystem>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/stat.h>

#include <chrono>

#include "core/frame_info.hpp"
#include "core/rpicam_app.hpp"
#include "core/still_options.hpp"

#include "output/output.hpp"

#include "image/image.hpp"

using namespace std::chrono_literals;
using namespace std::placeholders;
using libcamera::Stream;

namespace fs = std::filesystem;

class RPiCamStillApp : public RPiCamApp
{
public:
	RPiCamStillApp() : RPiCamApp(std::make_unique<StillOptions>()) {}

	StillOptions *GetOptions() const { return static_cast<StillOptions *>(options_.get()); }
};

/////////////////////////////////////////////////////////////////////
#include <iostream>
#include <opencv2/opencv.hpp> // OpenCV 라이브러리 포함
using libcamera::Stream;

/////////////////////////////////////////////////////////////////////

#include <cmath>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <tuple>

#include <ctime>
#include <curl/curl.h>

#include <cstdlib>
#include <string>

const int DEVICE_ID = []() -> int {
	const char * envVar = std::getenv("DEVICE_ID");
	if (envVar) {
		try {
			return std::stoi(envVar);
		} catch (const std::exception& e) {
			std::cerr << "conversion error: " << e.what() << std::endl;
		}
	}
	return 0;
}();

//debug 208~213
//debug 468~497
#define CKSFC 15
//radius of circle
#define BZV 100
//binaryzation value

// unsigned int count = 0;
unsigned int entry_count = 0;
unsigned int exit_count = 0;

std::queue<cv::Mat> frames;
std::queue<cv::Mat> convs;
std::queue<cv::Mat> firstDerivate;
std::queue<std::vector<std::tuple<cv::Point, int>>> objectCenter;
// int : state
// 0 : unknown(before matching)
// 1 : from outside
// 2 : from hive
std::queue<cv::Mat> originalImage;

std::mutex mtxCapConv;
std::mutex mtxConvFD;
std::mutex mtxtraceSD;
std::mutex mtxOCenter;

std::mutex mtxOriginalImage;

std::condition_variable condVar_capConv;
std::condition_variable condVar_convFD;
std::condition_variable condVar_traceSD;
std::condition_variable condVar_OCenter;

bool keepRunning = true;

const unsigned int targetFPS = 24; // 원하는 FPS
const unsigned int frameInterval = 1000 / targetFPS; // 프레임 간의 대기 시간 (밀리초)

cv::Mat circle;

cv::Mat capture;
cv::Mat afterConv;
cv::Mat afterLMF; //afterLM Filter
cv::Mat afterLM; //afterLocalMaximum
cv::Mat afterLMC; //after LM Casting

double top_crop_ratio = 0.4;
double bottom_crop_ratio = 0.1;
int original_height = 480;

int top_crop = static_cast<int>(original_height * top_crop_ratio);
int bottom_crop = static_cast<int>(original_height * bottom_crop_ratio);
int new_height = original_height - top_crop - bottom_crop;

double calculateEuclidDistance(const cv::Point &p1, const cv::Point &p2)
{
	return std::sqrt(std::pow(p1.x - p2.x, 2) + std::pow(p1.y - p2.y, 2));
}

cv::Rect roi(0, top_crop, 640, new_height);

cv::Mat createCircularKernel(int radius)
{
	// 커널의 크기 계산 (2 * radius + 1)
	int size = 2 * radius + 1;

	// size x size 크기의 행렬을 0으로 초기화
	cv::Mat kernel = cv::Mat::zeros(size, size, CV_8UC1);

	// 중심 좌표
	int center = radius;

	// 원을 그리기 위해 원의 방정식을 사용
	for (int i = 0; i < size; ++i)
	{
		for (int j = 0; j < size; ++j)
		{
			// 중심으로부터의 거리 계산
			int dx = i - center;
			int dy = j - center;
			if (dx * dx + dy * dy <= radius * radius)
			{
				// 원 내부의 점이면 1로 설정
				kernel.at<uchar>(i, j) = 1;
			}
		}
	}

	return kernel;
}

/////////////////////////////////////////////////////////////////////

// // 이미지를 캡처하고 OpenCV Mat으로 변환하는 함수
static cv::Mat save_image_as_mat(RPiCamStillApp &app, CompletedRequestPtr &payload, Stream *stream)
{
	StreamInfo info = app.GetStreamInfo(stream);
	BufferReadSync r(&app, payload->buffers[stream]);
	const std::vector<libcamera::Span<uint8_t>> mem = r.Get();

	// 이미지를 cv::Mat으로 변환
	// cv::Mat은 기본적으로 sallow copy를 함. clone을 하여 deep copy가 이뤄지도록 함.
	cv::Mat sallow = cv::Mat(info.height, info.width, CV_8UC3, (void *)mem[0].data());
	cv::Mat img = sallow.clone();

	return img;
}

// main 루프 내에서 호출하는 부분
static void event_loop(RPiCamStillApp &app)
{
	unsigned int still_flags = RPiCamApp::FLAG_STILL_NONE;
	still_flags |= RPiCamApp::FLAG_STILL_RGB;

	app.OpenCamera();
	app.ConfigureStill(still_flags);
	app.StartCamera();

	for (unsigned int count = 0;; count++)
	{
		auto loop_start = std::chrono::high_resolution_clock::now();
		RPiCamApp::Msg msg = app.Wait();
		if (msg.type == RPiCamApp::MsgType::RequestComplete)
		{
			CompletedRequestPtr &completed_request = std::get<CompletedRequestPtr>(msg.payload);
			// app.ShowPreview(completed_request, app.ViewfinderStream());
			// OpenCV Mat으로 변환하고 처리
			capture = save_image_as_mat(app, completed_request, app.StillStream());
			// cv::imshow("original image", capture);
		}
		// 나머지 처리...

		/////////////////////////////////////////////////////////////////////

		if (capture.empty())
		{
			std::cerr << "Error: Empty frame captured" << std::endl;
			continue;
		}

		// 블루 채널만 추출하여 그레이스케일 이미지로 변환
		cv::Mat crop = capture(roi);
		
		// 만약 평균이 120보다 작으면 무시(어두운 이미지)
		auto m = cv::mean(crop);
		// std::cout << m << std::endl;
		if (m[0] < 120)
		{
			continue;
		}

		{
			originalImage.push(crop);
		}
		std::vector<cv::Mat> channels(3);
		cv::split(crop, channels);
		crop = channels[0];

		// 이진화 (임계값 80 기준으로 이진화)
		cv::Mat binaryImage;
		// cv::threshold(crop, binaryImage, 80, 255, cv::THRESH_BINARY_INV);
		// binaryImage = binaryImage / 255;
		cv::threshold(crop, binaryImage, BZV, 1, cv::THRESH_BINARY_INV);

		//20240628 추가
		{
			binaryImage.convertTo(binaryImage, CV_16UC1);
		}

		// 큐에 프레임 추가
		{
			std::lock_guard<std::mutex> lock(mtxCapConv);
			frames.push(binaryImage);
		}
		// std::cout << "frame queue size : " << frames.size() << std::endl;

		condVar_capConv.notify_all();

		auto loop_end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed = loop_end - loop_start;
		// std::cout << "Elapsed time: " << elapsed.count() << " seconds" << std::endl;

		if (elapsed.count() < frameInterval)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(frameInterval) - elapsed);
		}
	}
}

void binConvImage()
{
	while (true)
	{
		// auto loop_start = std::chrono::high_resolution_clock::now();

		cv::Mat frame;

		// 큐에서 프레임을 꺼내기
		{
			std::unique_lock<std::mutex> lock(mtxCapConv);
			condVar_capConv.wait(lock, [] { return !frames.empty() || !keepRunning; });

			if (!keepRunning && frames.empty())
				break;

			frame = frames.front();
			frames.pop();
		}

		// image process

		// cv::filter2D(frame, afterConv, CV_8UC1, circle);
		//20240628 수정
		cv::filter2D(frame, afterConv, CV_16UC1, circle);

		{
			//binary image show
			//Use only in degugging
			// cv::imshow("Binary Image", frame * 255);
			// cv::waitKey(1); // 짧은 대기 시간으로 업데이트
		}
		// 이미지 처리 (여기서는 단순히 화면에 표시)
		// cv::imshow("Binary Image", frame * 255);
		// cv::imshow("Processed Image", afterConv);
		// cv::waitKey(1); // 짧은 대기 시간으로 업데이트

		{
			std::lock_guard<std::mutex> lock(mtxConvFD);
			convs.push(afterConv);
		}
		// std::cout << "__convs queue size : " << convs.size() << std::endl;

		condVar_convFD.notify_all();

		// auto loop_end = std::chrono::high_resolution_clock::now();
		// std::chrono::duration<double> elapsed = loop_end - loop_start;
	}
}

void convFirstDerivate()
{
	while (true)
	{
		// auto loop_start = std::chrono::high_resolution_clock::now();

		cv::Mat frame;

		{
			std::unique_lock<std::mutex> lock(mtxConvFD);
			condVar_convFD.wait(lock, [] { return !convs.empty() || !keepRunning; });

			if (!keepRunning && convs.empty())
				break;

			frame = convs.front();
			convs.pop();
		}

		for (int col = 1; col < frame.cols - 1; col++)
		{
			for (int row = 0; row < frame.rows - 1; row++)
			{
				if ((frame.at<uint16_t>(row, col) < frame.at<uint16_t>(row + 1, col)))
				{
					frame.at<uint16_t>(row, col) = 0;
				}
			}
		}
		for (int col = 1; col < frame.cols - 1; col++)
		{
			for (int row = frame.rows - 2; row > 0; row--)
			{
				if ((frame.at<uint16_t>(row, col) <= frame.at<uint16_t>(row - 1, col)))
				{
					frame.at<uint16_t>(row, col) = 0;
				}
			}
		}

		// cv::imshow("first_derivate",frame * 128);
		// cv::waitKey(1); // 짧은 대기 시간으로 업데이트

		{
			std::lock_guard<std::mutex> lock(mtxtraceSD);
			firstDerivate.push(frame);
		}

		condVar_traceSD.notify_all();

		// auto loop_end = std::chrono::high_resolution_clock::now();
		// std::chrono::duration<double> elapsed = loop_end - loop_start;
	}

	cv::destroyAllWindows();
}

void traceSecondDerivate()
{
	uint16_t coor_v;
	unsigned int coor_h;
	unsigned int coor_w;

	uint16_t last_coor_v;
	unsigned int last_coor_h;
	unsigned int last_coor_w;

	unsigned int line_size;
	unsigned int flag;
	unsigned int count;

	while (true)
	{
		// auto loop_start = std::chrono::high_resolution_clock::now();

		cv::Mat frame;
		cv::Mat check;

		cv::Mat center;

		count = 0;

		// std::vector<cv::Point> centers;
		std::vector<std::tuple<cv::Point, int>> centers;

		{
			std::unique_lock<std::mutex> lock(mtxtraceSD);
			condVar_traceSD.wait(lock, [] { return !firstDerivate.empty() || !keepRunning; });

			if (!keepRunning && firstDerivate.empty())
				break;

			frame = firstDerivate.front();
			firstDerivate.pop();
		}

		frame.convertTo(check, CV_8UC1);

		cv::threshold(check, check, 50, 1, cv::THRESH_BINARY_INV);

		// cv::imshow("frame_image",frame * 128);
		// cv::imshow("check_matrix",check * 100);

		center = cv::Mat::zeros(check.size(), check.type());
		for (int row = 5; row < frame.rows - 5; row++)
		{
			for (int col = 1; col < frame.cols - 1; col++)
			{
				if (check.at<uchar>(row, col) != 0)
					continue;
				check.at<uchar>(row, col) = 1;
				coor_h = row;
				coor_w = col;
				coor_v = frame.at<uint16_t>(row, col);
				line_size = 1;
				while (1)
				{
					flag = 1;
					if (coor_w > frame.cols - 2)
						break;
					for (int k = -3; k < 4; k++)
					{
						// if(coor_h+k < 0)continue;
						if (coor_h + k >= frame.rows)
							continue;

						//20240630 edit(nabi)
						if (check.at<uchar>(coor_h + k, coor_w + 1) != 0)
							continue;
						check.at<uchar>(coor_h + k, coor_w + 1) = 1;
						if (frame.at<uint16_t>(coor_h + k, coor_w + 1) != 0)
						{
							last_coor_h = coor_h;
							last_coor_w = coor_w;
							last_coor_v = coor_v;

							coor_h = coor_h + k;
							coor_w = coor_w + 1;
							coor_v = frame.at<uint16_t>(coor_h, coor_w);

							flag = 0;
							line_size++;
							break;
						}
					}
					if (flag)
						break;
					if (last_coor_v > coor_v)
					{
						if (line_size < 5)
							break;
						line_size = 1;
						count++;
						center.at<uchar>(last_coor_h, last_coor_w) = 1;
						//save_coordinate
						// centers.push_back(cv::Point(last_coor_h, last_coor_w));
						centers.push_back(std::make_tuple(cv::Point(last_coor_w, last_coor_h), 0));
					}
				}
			}
		}

		for (int i = 0; i < centers.size(); i++)
		{
			for (int j = i + 1; j < centers.size(); j++)
			{
				double distance = calculateEuclidDistance(std::get<0>(centers[i]), std::get<0>(centers[j]));
				// std::cout << distance << " / ";
				if (distance < 11)
				{
					centers.erase(centers.begin() + j);
					i--;
					j--;
				}
			}
		}
		// std::cout<<std::endl;

		// cv::imshow("center",center * 100);
		// cv::waitKey(1); // 짧은 대기 시간으로 업데이트

		{
			std::lock_guard<std::mutex> lock(mtxOCenter);
			objectCenter.push(centers);
		}
		// std::cout << "______object center queue size : " << objectCenter.size() << std::endl;

		condVar_OCenter.notify_all();

		// centers.clear();

		// auto loop_end = std::chrono::high_resolution_clock::now();
		// std::chrono::duration<double> elapsed = loop_end - loop_start;
	}
}

void calculateDistance()
{
	std::string url = "http://wcl.inu.ac.kr/honeybee/api/uplink";
	int frameCount = 0;

	// std::vector<cv::Point> previous;
	// std::vector<cv::Point> now;
	std::vector<std::tuple<cv::Point, int>> previous;
	std::vector<std::tuple<cv::Point, int>> now;

	cv::Mat pointImage;
	while (true)
	{
		{
			std::unique_lock<std::mutex> lock(mtxOCenter);
			condVar_OCenter.wait(lock, [] { return !objectCenter.empty() || !keepRunning; });

			if (!keepRunning && objectCenter.empty())
				break;

			now = objectCenter.front();
			objectCenter.pop();
		}

		// for(int i=0;i<now.size();i++){
		//     std::cout << std::get<0>(now[i]).y << " / " << std::get<0>(now[i]).x << " : ";
		// }
		// std::cout<<std::endl;

		if (previous.empty() && now.empty())
			continue;

		using distanceTuple = std::tuple<double, int, int>; //distance, previous, now index
		std::priority_queue<distanceTuple, std::vector<distanceTuple>, std::greater<distanceTuple>> priorityQueue;

		for (size_t i = 0; i < previous.size(); i++)
		{
			for (size_t j = 0; j < now.size(); j++)
			{
				// double distance = calculateEuclidDistance(previous[i],now[j]);
				double distance = calculateEuclidDistance(std::get<0>(previous[i]), std::get<0>(now[j]));
				priorityQueue.push(std::make_tuple(distance, i, j));
			}
		}

		std::vector<std::tuple<int, int, double>> matches; //previous, now index, distance

		std::vector<bool> usedPrevious(previous.size(), false);
		std::vector<bool> usedNow(now.size(), false);

		while (!priorityQueue.empty())
		{
			auto [distance, prevIndex, nowIndex] = priorityQueue.top();
			priorityQueue.pop();

			if (distance > static_cast<int>(new_height * 0.6))
				continue; //이동거리가 크롭이미지 상하 폭보다 길지 않다고 가정

			if (!usedPrevious[prevIndex] && !usedNow[nowIndex])
			{
				// std::cout << distance << " / ";

				matches.push_back(std::make_tuple(prevIndex, nowIndex, distance));
				usedPrevious[prevIndex] = true;
				usedNow[nowIndex] = true;
			}
		}
		// std::cout << std::endl;

		{
			/*
                        image show code
                        Use this code in debug mode only
                    */
			pointImage = originalImage.front();
			originalImage.pop();
			cv::circle(pointImage, cv::Point(CKSFC, CKSFC), CKSFC, cv::Scalar(0, 0, 0), -1);

			//tracking : blue
			for (int i = 0; i < matches.size(); i++)
			{
				cv::line(pointImage, std::get<0>(previous[std::get<0>(matches[i])]),
						 std::get<0>(now[std::get<1>(matches[i])]), cv::Scalar(0, 255, 0), 3);
			}

			//previous point : yellow
			for (int i = 0; i < previous.size(); i++)
			{
				cv::circle(pointImage, std::get<0>(previous[i]), 4, cv::Scalar(0, 255, 255), -1);
			}

			//now point : red
			for (int i = 0; i < now.size(); i++)
			{
				cv::circle(pointImage, std::get<0>(now[i]), 4, cv::Scalar(0, 0, 255), -1);
			}

			// cv::imshow("pointImage",pointImage);
			// cv::waitKey(1);
		}

		for (size_t i = 0; i < usedNow.size(); ++i)
		{
			if (!usedNow[i])
			{
				// false인 경우 수행할 특정 동작
				if (std::get<0>(now[i]).y < new_height / 2)
					std::get<1>(now[i]) = 1;
				else
					std::get<1>(now[i]) = 2;
				// 예제: false 값을 true로 변경
				usedNow[i] = true;
			}
			else
			{
				for (auto it = matches.begin(); it != matches.end(); ++it)
				{
					if (std::get<1>(*it) == i)
					{
						std::get<1>(now[i]) = std::get<1>(previous[std::get<0>(*it)]);
						matches.erase(it);
						break;
					}
				}
			}
		}

		for (size_t i = 0; i < usedPrevious.size(); i++)
		{
			if (!usedPrevious[i])
			{
				std::tuple<cv::Point, int> tempTuple = previous[i];
				if (std::get<1>(tempTuple) == 1)
				{
					if (std::get<0>(tempTuple).y >= new_height / 2)
						entry_count++;
				}
				if (std::get<1>(tempTuple) == 2)
				{
					if (std::get<0>(tempTuple).y < new_height / 2)
						exit_count++;
				}
			}
		}

		// std::cout << "Count / Entry / Exit : " << now.size() << " / " << entry_count << " / " << exit_count << std::endl;
		frameCount++;
		if (frameCount > 500)
		{
			// 현재 시간 가져오기
			std::time_t now = std::time(nullptr);
			std::tm localTime = *std::localtime(&now);

			// 현재 날짜와 시간 포맷팅
			std::ostringstream oss;
			oss << "/home/pi/Pictures/" << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S") << ".jpg";

			std::string filename = oss.str();

			// 이미지를 파일로 저장
			if (cv::imwrite(filename, pointImage))
			{
				std::cout << "Image saved to: " << filename << std::endl;
			}
			else
			{
				std::cerr << "Failed to save image to: " << filename << std::endl;
			}
		}
		if (frameCount > 500)
		{
			std::string jsonData = "{"
								   "\"id\": " +
								   std::to_string(DEVICE_ID) +
								   ","
								   "\"type\": " +
								   std::to_string(3) +
								   ","
								   "\"inField\": " +
								   std::to_string(exit_count) +
								   ","
								   "\"outField\": " +
								   std::to_string(entry_count) + "}";

			// libcurl 초기화
			CURL *curl;
			CURLcode res;

			curl = curl_easy_init();
			if (curl)
			{
				// 요청 설정
				curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
				curl_easy_setopt(curl, CURLOPT_POST, 1L);
				curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());

				// 헤더 설정 (JSON 전송)
				struct curl_slist *headers = NULL;
				headers = curl_slist_append(headers, "Content-Type: application/json");
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

				// 요청 실행
				res = curl_easy_perform(curl);

				// 결과 확인
				if (res != CURLE_OK)
				{
					std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
				}
				else
				{
					long response_code;
					curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
					std::cout << " Response Code: " << response_code << std::endl;
				}

				// 리소스 정리
				curl_slist_free_all(headers);
				curl_easy_cleanup(curl);
			}
			frameCount = 0;
			entry_count = 0;
			exit_count = 0;
		}

		previous.clear();
		previous = now;
		now.clear();
	}
}

/////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
	try
	{
		RPiCamStillApp app;
		StillOptions *options = app.GetOptions();

		/////////////////////////////////////////////////////////////////////

		// circle = createCustomKernel();
		circle = createCircularKernel(CKSFC);

		std::cout << "detected DEVICE_ID: " << std::to_string(DEVICE_ID) << std::endl;

		/////////////////////////////////////////////////////////////////////

		if (options->Parse(argc, argv))
		{
			if (options->verbose >= 2)
				options->Print();

			// event_loop(app);
			// 이미지 캡처 스레드 시작
			// std::thread captureThread(captureImage);
			std::thread captureThread(event_loop, std::ref(app));
			// binary image에서 convolution
			std::thread binConvThread(binConvImage);
			// convolution image에서 point detect를 위한 first derivation
			std::thread convFirstDerivateThread(convFirstDerivate);
			std::thread traceSecondDerivateThread(traceSecondDerivate);
			std::thread calculateDistanceThread(calculateDistance);

			// 사용자가 키를 누를 때까지 대기
			std::cout << "Press Enter to stop..." << std::endl;
			std::cin.get();
			keepRunning = false;

			// 큐 비우기 및 종료 신호
			condVar_capConv.notify_all();
			condVar_convFD.notify_all();
			condVar_traceSD.notify_all();
			condVar_OCenter.notify_all();

			// 스레드 종료 대기
			captureThread.join();
			binConvThread.join();
			convFirstDerivateThread.join();
			traceSecondDerivateThread.join();
			calculateDistanceThread.join();
		}
	}
	catch (std::exception const &e)
	{
		LOG_ERROR("ERROR: *** " << e.what() << " ***");
		return -1;
	}
	return 0;
}
