/*
 * Future Technology Devices International Limited
 *
 * File transfer is tested using read() & write() Synchronous API's using
 * threads in loopback mode. User has to make sure that proper
 * synchronisation between read and write threads.
 *
 * Read pipe without writing into pipe may leads to TIMEOUT errors.
 *
 */

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <random>
#include <thread>
#include "common.hpp"

static bool loop_mode;
static bool is_ft600_mode;
static const uint32_t WR_CTRL_INTERVAL = 5000;
static const uint32_t RD_CTRL_INTERVAL = 5000;
static const int BUFFER_LEN = 128*1024;
static random_device rd;
static mt19937 rng(rd());
static uniform_int_distribution<size_t> random_len(1, BUFFER_LEN / 4);
static size_t file_length;
static bool transfer_failed;

std::mutex g_mutex;
std::condition_variable g_cv;
bool g_ready = false;
bool rd_fail_flag = false;
bool wr_fail_flag = false;

static void stream_out(FT_HANDLE handle, uint8_t channel, string from)
{
	printf("App: func:%s line:%d \n", __func__, __LINE__);
	std::unique_lock<std::mutex> ul(g_mutex);

	unique_ptr<uint8_t[]> buf(new uint8_t[BUFFER_LEN]);
	ifstream src;
	try {
		src.open(from, ios::binary);
	} catch (istream::failure e) {
		cout << "Failed to open file " << e.what() << endl;
		return;
	}
	size_t total = 0;

	while (!do_exit && total < file_length) {

		size_t len = 1024 * 16;
		ULONG count = 0;

		src.read((char*)buf.get(), len);
		if (!src)
			len = (int)src.gcount();

		//printf("App: funch:%s line:%d len=%ld total=%ld\n", __func__, __LINE__, len, total);
		if (len) {
			FT_STATUS status = FT_WritePipeEx(handle, channel, buf.get(),
						len, &count, 0);
			if (FT_OK != status) {
				if (do_exit)
					break;
				printf("Channel %d failed to write %zu, ret %d\r\n", channel, total, status);
				do_exit = true;

				g_ready = true;
				ul.unlock();
				g_cv.notify_one();//Notify Read() that write finished.
				ul.lock();

				return;
			}
			//printf("App: funch:%s line:%ld len=%d count=%ld\n", __func__, __LINE__, len, count);
		} else
			count = 0;

		tx_count += count;
		total += count;
		std::this_thread::yield();

		g_ready = true;
		ul.unlock();
		g_cv.notify_one();//Notify Read() that write finished.
		ul.lock();

		if (!rd_fail_flag) //If read fail, dont wait for read...
		{
			//printf("%s: Wait Till Read finish...\n", __func__);
			g_cv.wait(ul, []() { return g_ready == false; });//Wait Till Read finish.
		}
	}

	src.close();

	printf("Channel %d write stopped, %zu\r\n", channel, total);
}

static void stream_in(FT_HANDLE handle, uint8_t channel, string to)
{
	printf("App: funch:%s line:%d \n", __func__, __LINE__);

	unique_ptr<uint8_t[]> buf(new uint8_t[BUFFER_LEN]);
	ofstream dest;
	size_t total = 0;

	try {
		dest.open(to, ofstream::binary | ofstream::in | ofstream::out |
				ofstream::trunc);
	} catch (istream::failure e) {
		cout << "Failed to open file " << e.what() << endl;
		return;
	}

	while (!do_exit && total < file_length) {
		std::unique_lock<std::mutex> ul(g_mutex);
		//printf("App: read: before g_cv.wait() **Wait for write complete first..** \n");

		if (!wr_fail_flag) {
			//printf("%s: Wait Till Write finish...\n", __func__);
			g_cv.wait(ul, []() { return g_ready; });//Wait Till write finish..
		}
		ULONG count = 0;
		size_t len = 1024 * 16;
		size_t left = file_length - total;

		if (len > left)
			len = left;

		//printf("App: func:%s line:%d len=%ld total=%ld\n", __func__, __LINE__, len, total);
		FT_STATUS status = FT_ReadPipeEx(handle, channel, buf.get(), len, &count, 0);
		if (!count) {
			printf("Channel %d failed to read %zu, ret %d\r\n", channel, total, status);
			g_ready = false;
			ul.unlock();
			g_cv.notify_one();//Notify write that Read finished.
			rd_fail_flag = true;

			return;
		}
		dest.write((const char *)buf.get(), count);
		rx_count += count;
		total += count;

                g_ready = false;
                //printf("App: read: before ul.unlock()\n");
                ul.unlock();
                //printf("App: read: before g_cv.notify_one()\n");
                g_cv.notify_one();//Notify write that Read finished.
	}

	dest.close();

	printf("Channel %d read stopped, %zu\r\n", channel, total);
}

static void show_help(const char *bin)
{
	printf("File transfer through FT245 loopback FPGA\r\n");
	printf("Usage: %s <src> <dest> <mode> [loop]\r\n", bin);
	printf("  src: source file name to read\r\n");
	printf("  dest: target file name to write\r\n");
	printf("  mode: 0 = FT245 mode(default), 1-4 FT600 channel count\r\n");
	printf("  loop: 0 = oneshot(default), 1 =  loop forever\r\n");
}

static bool validate_arguments(int argc, char *argv[])
{
	if (argc != 4 && argc != 5)
		return false;

	if (argc == 5) {
		int val = atoi(argv[4]);
		if (val != 0 && val != 1)
			return false;
		loop_mode = (bool)val;
	}

	int ch_cnt = atoi(argv[3]);
	if (ch_cnt > 4)
		return false;

	is_ft600_mode = ch_cnt != 0;

	in_ch_cnt = out_ch_cnt = ch_cnt;
	//printf("App: in_ch_cnt=%d\n", in_ch_cnt);

	return true;
}

static inline ifstream::pos_type get_file_length(ifstream &stream)
{
	return stream.seekg(0, ifstream::end).tellg();
}

static inline ifstream::pos_type get_file_length(const string &name)
{
	ifstream file(name, std::ifstream::binary);

	return get_file_length(file);
}

static bool compare_content(const string &from, const string &to)
{
	printf("\nApp: func:%s line:%d\n", __func__, __LINE__);
	ifstream in1(from);
	ifstream in2(to);
	ifstream::pos_type size1, size2;

	size1 = get_file_length(in1);
	in1.seekg(0, ifstream::beg);

	size2 = get_file_length(in2);
	in2.seekg(0, ifstream::beg);

	printf("############################################\n");
	if(size1 != size2) {
		cout << to << " size not same: " << size1 << " " << size2 << endl;
		in1.close();
		in2.close();
		return false;
	}

	static const size_t BLOCKSIZE = 4096;
	size_t remaining = size1;

	while(remaining) {
		char buffer1[BLOCKSIZE], buffer2[BLOCKSIZE];
		size_t size = std::min(BLOCKSIZE, remaining);

		in1.read(buffer1, size);
		in2.read(buffer2, size);

		if(0 != memcmp(buffer1, buffer2, size)) {
			for (size_t i = 0; i < size; i++) {
				if (buffer1[i] != buffer2[i]) {
					size_t offset = (int)size1 - (int)remaining + i;
					cout << to << " content not same at " << offset << endl;
					break;
				}
			}
			in1.close();
			in2.close();
			return false;
		}

		remaining -= size;
	}
	in1.close();
	in2.close();
	cout << from << " & " << to << " binary same" << endl;
	printf("############################################");
	return true;
}

void file_transfer(FT_HANDLE handle, uint8_t channel, string from, string to)
{
	printf("App: %s %d channel=%d \n\n", __func__, __LINE__, channel);
	do {
		thread write_thread = thread(stream_out, handle, channel, from);
		thread read_thread = thread(stream_in, handle, channel, to);

		if (write_thread.joinable())
			write_thread.join();
		if (read_thread.joinable())
			read_thread.join();

		if (!compare_content(from, to))
			transfer_failed = true;
	} while (loop_mode && !do_exit);
}

int main(int argc, char *argv[])
{
	get_version();

	if (!validate_arguments(argc, argv)) {
		show_help(argv[0]);
		return -1;
	}

	if (!get_device_lists(500))
		return -1;

	/* Must be called before FT_Create is called */
	turn_off_thread_safe();

	FT_HANDLE handle = NULL;

	FT_Create(0, FT_OPEN_BY_INDEX, &handle);

	if (!handle) {
		printf("Failed to create device\r\n");
		return -1;
	}
	printf("Create device SUCCESS!!\n\n");
	register_signals();

	for (int i = 0; i < in_ch_cnt; i++) {
		FT_SetPipeTimeout(handle, 2 + i, WR_CTRL_INTERVAL + 100);
		FT_SetPipeTimeout(handle, 0x82 + i, RD_CTRL_INTERVAL + 100);
	}

	thread transfer_thread[4];

	string from(argv[1]);
	string to(argv[2]);

	file_length = get_file_length(from);

	if (file_length == 0) {
		cout << "Input file not correct" << endl;
		return -1;
	}
	printf("App: file_length=%ld\n\n", file_length);

	for (int i = 0; i < in_ch_cnt; i++) {
		string target = to;
		if (in_ch_cnt > 1)
			target += to_string(i);
		transfer_thread[i] = thread(file_transfer, handle, i, from, target);
	}

	for (int i = 0; i < in_ch_cnt; i++)
		transfer_thread[i].join();

	do_exit = true;

	FT_Close(handle);

	return 0;
}
