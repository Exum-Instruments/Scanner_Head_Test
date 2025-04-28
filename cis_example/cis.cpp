#include "pch.h"
#include "cis_core.h"
#include "cis_img.h"
#include <chrono>
#include <conio.h>
#include <iostream>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <time.h>

using namespace winrt;
using namespace Windows::Foundation;

#define SOURCE_REMOVE 0
#define SOURCE_CONTINUE 1
// Global variables
unsigned char* pixels;
std::thread usb_config_thread;
int buf_h1 = 1280;
int color_type = 0;
int w1 = 1288;
int h1 = 1280;

// Configuration structure
struct CISConfig {
	int color_type;     // Color mode
	int isp;            // Image Signal Processing
	int gamma;          // Gamma correction
	int mode;           // Scan mode
	int num_images;     // Number of images to capture
	// Motor control parameters
	//int motor_enable;    // Enable motor (0=off, 1=on)
	//int motor_direction; // Motor direction (0=forward, 1=reverse)
	//int motor_step_freq; // Motor step frequency (Hz)
	//int motor_step_duty; // Motor step duty cycle (%)
};

// Default configuration
CISConfig config = { 0, 0, 0, 0, 1 };

// BMP file header structure
#pragma pack(push, 1)
typedef struct {
	uint16_t bfType;      // Magic identifier: 0x4D42 ("BM")
	uint32_t bfSize;      // File size in bytes
	uint16_t bfReserved1; // Reserved
	uint16_t bfReserved2; // Reserved
	uint32_t bfOffBits;   // Offset to pixel data
} BMPFileHeader;

// BMP information header structure
typedef struct {
	uint32_t biSize;          // Header size in bytes
	int32_t biWidth;          // Width of the image
	int32_t biHeight;         // Height of the image
	uint16_t biPlanes;        // Number of color planes
	uint16_t biBitCount;      // Bits per pixel (24 for RGB)
	uint32_t biCompression;   // Compression type (0 = uncompressed)
	uint32_t biSizeImage;     // Image size in bytes
	int32_t biXPelsPerMeter;  // Pixels per meter (X-axis)
	int32_t biYPelsPerMeter;  // Pixels per meter (Y-axis)
	uint32_t biClrUsed;       // Number of colors used
	uint32_t biClrImportant;  // Important colors
} BMPInfoHeader;
#pragma pack(pop)

// Function prototypes
//void set_motor_control(int enable, int direction, int step_freq, int step_duty);
//void start_motor(int direction, int step_freq, int step_duty);
//void stop_motor();
void initialize_scanner();
void display_color_options();
int get_int_input(int min_val, int max_val);

// Set motor control parameters using appropriate registers
//void set_motor_control(int enable, int direction, int step_freq, int step_duty) {
//	printf("Setting motor parameters - Enable: %d, Direction: %d, Frequency: %d, Duty: %d\n",
//		enable, direction, step_freq, step_duty);
//
//
//
//	std::cout << "Res set to " << cis_i2c_read(I2C_ADDR, 0x0107) << std::endl;
//	std::cout << "Trigger mode set to " << cis_i2c_read(I2C_ADDR, 0x0401) << std::endl;
//	std::cout << "Operation mode set to " << cis_i2c_read(I2C_ADDR, 0x0400) << std::endl;
//
//	// Write to motor control registers as defined in the CIS Engine documentation
//	cis_i2c_write(I2C_ADDR, 0x402, enable);       // Motor Enable
//	cis_i2c_write(I2C_ADDR, 0x403, direction);    // Motor Direction
//	cis_i2c_write(I2C_ADDR, 0x404, step_freq);    // Motor Step (Frequency)
//	cis_i2c_write(I2C_ADDR, 0x405, step_duty);    // Motor Step (Duty Cycle)
//	// Start scanning
//	printf("Motor set!              Press any key to start scanning...");
//	_getch();
//	std::cout << "DIRECTION mode set to " << cis_i2c_read(I2C_ADDR, 0x403) << std::endl;
//
//}

// Start motor with specified parameters
//void start_motor(int direction, int step_freq, int step_duty) {
//	printf("Starting motor...\n");
//	set_motor_control(1, direction, step_freq, step_duty);
//}

// Stop motor
//void stop_motor() {
//	printf("Stopping motor...\n");
//	set_motor_control(0, 0, 0, 0);
//}

// Function to save image as BMP file
void save_bmp(const char* filename, unsigned char* pix, int width, int height) {
	FILE* fp = nullptr;
	errno_t err = fopen_s(&fp, filename, "wb");
	if (err != 0) {
		fprintf(stderr, "Error: Unable to create file %s\n", filename);
		return;
	}

	int row_padded = (width * 3 + 3) & (~3);
	int file_size = 54 + row_padded * height;

	// Setup file header
	BMPFileHeader file_header;
	file_header.bfType = 0x4D42;
	file_header.bfSize = file_size;
	file_header.bfReserved1 = 0;
	file_header.bfReserved2 = 0;
	file_header.bfOffBits = 54;

	// Setup info header
	BMPInfoHeader info_header;
	info_header.biSize = sizeof(BMPInfoHeader);
	info_header.biWidth = width;
	info_header.biHeight = -height;  // Negative for top-down image
	info_header.biPlanes = 1;
	info_header.biBitCount = 24;
	info_header.biCompression = 0;
	info_header.biSizeImage = row_padded * height;
	info_header.biXPelsPerMeter = 2835;
	info_header.biYPelsPerMeter = 2835;
	info_header.biClrUsed = 0;
	info_header.biClrImportant = 0;

	// Write BMP headers
	fwrite(&file_header, sizeof(file_header), 1, fp);
	fwrite(&info_header, sizeof(info_header), 1, fp);

	// Write pixel data (row by row with padding)
	unsigned char padding[3] = { 0, 0, 0 };
	for (int y = 0; y < height; y++) {
		fwrite(pix + y * width * 3, 3, width, fp);
		fwrite(padding, 1, (row_padded - width * 3), fp);
	}

	fclose(fp);
	printf("Image saved successfully as %s\n", filename);
}

// Convert RGB to BGR (BMP format requires BGR)
void swap_rgb_to_bgr(unsigned char* pix, int width, int height) {
	int num_pixels = width * height * 3;
	unsigned char temp;
	for (int i = 0; i < num_pixels; i += 3) {
		temp = pix[i]; // Swap red and blue 
		pix[i] = pix[i + 2];
		pix[i + 2] = temp;
	}
}

// Generate filename and save image
void save_image(int width, int height) {
	char filename[100];
	time_t current_time = time(NULL);
	struct tm timeinfo;
	errno_t err = localtime_s(&timeinfo, &current_time);
	if (err == 0) {
		strftime(filename, sizeof(filename), "scan_%Y%m%d_%H%M%S.bmp", &timeinfo);
	}
	else {
		sprintf(filename, "scan_image.bmp");
		fprintf(stderr, "Error converting time.\n");
	}
	save_bmp(filename, pixels, width, height);
}

// USB configuration thread function
void cis_usb_setup() {
	cis_usb_config();
}

// Set color mode for scanner
void set_color(int index) {
	color_type = index;
	int dta = 0;
	cis_i2c_write(I2C_ADDR, 0x301, index);

	if (index < 6) {
		dta = index;
	}
	else if (index == 7) {
		buf_h1 *= 2;
		dta = 7;
	}
	else if (index < 10) {
		if (index == 8) {
			buf_h1 *= 3;
		}
		dta = 5;
	}
	else {
		dta = 6;
	}

	color_type = index;
	cis_i2c_write(I2C_ADDR, 0x301, dta);
}

// Enable/disable Image Signal Processing
void set_isp(int enabled) {
	if (enabled) {
		enableISP = 1;
		cis_i2c_write(I2C_ADDR, 0x200, 0x02);
	}
	else {
		enableISP = 0;
		cis_i2c_write(I2C_ADDR, 0x200, 0x00);
	}
}

// Enable/disable Gamma correction
void set_gamma(int enabled) {
	enableGamma = enabled;
}

// Set scan mode (streaming or scan)
//void setScanMode(int mode) {
//	cis_i2c_write(I2C_ADDR, 0x408, mode);
//	printf("Scan mode set to: %s\n", mode ? "Scan Mode" : "Stream Mode");
//}

// Capture an image from the scanner

bool captureImage() {
	if (config.num_images <= 0) {
		return false;
	}
	if (checkFrameData()) {
		getImage(pixels, color_type);
		swap_rgb_to_bgr(pixels, w1 - 8, h1);
		save_image(w1 - 8, h1);
		config.num_images--;
		return true;
	}
	else {
		printf(".");
		Sleep(100);
	}
	if (done) {
		return SOURCE_REMOVE;
	}
	return true;
}



// Returns true if the first line's header indicates an external trigger (0xA33B)
//  bool check_Ext_Trigger()
//{
//	if (checkFrameData()) {
//	if (checkFrameData()) {
//		// Allocate buffer for one frame (w1 lines, 1280 bytes per line)
//		unsigned char* pRaw = (unsigned char*)malloc(w1 * 1280);
//		if (!pRaw) {
//			fprintf(stderr, "Memory allocation failed in check_Ext_Trigger()\n");
//			return false;
//		}
//		// Attempt to get a raw image frame
//		int lines_read = getRawImage(pRaw, 0, 0, (short)w1, 1280);
//		if (lines_read <= 0) {
//			fprintf(stderr, "getRawImage() failed or returned no data\n");
//			free(pRaw);
//			return false;
//		}
//
//		if (pRaw[1] == 59) {
//			free(pRaw);
//			return true; // External trigger detected
//		}
//		free(pRaw);
//		return false; // No external triggers
//	}
//}

// Initialize and start the scanning process with motor control
void start_capture() {
	printf("\nInitializing scanner...\n");

	fSync = 0;
	cis_i2c_write(I2C_ADDR, 0x202, buf_h1);
	int w = cis_i2c_read(I2C_ADDR, 0x205);
	if (w != 0xFFFF) {
		int h = cis_i2c_read(I2C_ADDR, 0x202);
		w1 = w;
		h1 = h;
	}
	else {
		w1 = 1280;
		h1 = 1280;
	}
	cis_init(1288, h1, 1);

	if (color_type == 7) // RGBG
	{
		h1 = h1 >> 1;
	}
	else if (color_type == 8) //RGBH
	{
		h1 = h1 / 3;
	}

	printf("Scanning in progress...\n");

	int line_rate = 0x3C5E;
	cis_i2c_write(I2C_ADDR, 0x104, line_rate);
	std::cout << "Line Rate set to " << cis_i2c_read(I2C_ADDR, 0x104) << std::endl;

	setScanMode(1);
	cis_usb_start();
	int count = 0;
	while (1) {
		Sleep(200);
		printf("Loop : %d\n", count);
		if (!captureImage()) {
			printf("Scan complete!\n");
			break;
		}
		count++;
	}

	//// Stop motor after scanning is complete
	//if (config.motor_enable) {
	//	stop_motor();
	//}
}


// Display color mode options
void display_color_options() {
	printf("\nColor Mode Options:\n");
	printf("  0: Off\n");
	printf("  1: Red\n");
	printf("  2: Green\n");
	printf("  3: Blue\n");
	printf("  4: IR\n");
	printf("  5: RGB (Full Color)\n");
	printf("  6: RGBGRW\n");
	printf("  7: RGBG\n");
	printf("  8: RGBH (unstable)\n");
	printf("  9: Grayscale\n");
	printf(" 10: Black & White\n");
}

// Display main menu
void display_main_menu() {
	printf("\n=== Scanner Control Menu ===\n");
	printf("1. Quick Scan (Default Settings)\n");
	printf("2. Custom Scan (Advanced Settings)\n");
	printf("3. Exit\n");
	printf("Enter your choice (1-3): ");
}

// Get integer input from user with validation
int get_int_input(int min_val, int max_val) {
	int value;
	while (true) {
		if (scanf_s("%d", &value) != 1) {
			// Clear input buffer
			while (getchar() != '\n');
			printf("Invalid input. Please enter a number: ");
		}
		else if (value < min_val || value > max_val) {
			printf("Please enter a value between %d and %d: ", min_val, max_val);
		}
		else {
			// Clear remaining input buffer
			while (getchar() != '\n');
			return value;
		}
	}
}

// Initialize scanner hardware
void initialize_scanner() {
	printf("Initializing scanner hardware...\n");

	cis_usb_init();
	cis_i2c_write(I2C_ADDR, 0x108, 1600);
	int maxVal = cis_i2c_read(I2C_ADDR, 0x209);
	create_gamma((unsigned short)maxVal);

	// Start USB configuration thread
	usb_config_thread = std::thread(cis_usb_setup);

	// Allocate memory for image data
	pixels = (unsigned char*)malloc(1288 * 1280 * 3);

	printf("Scanner initialized successfully.\n");
}

// Custom scan mode with user-defined settings including motor control
void custom_scan_mode() {
	printf("\n=== Custom Scan Mode ===\n");

	// Get color mode
	display_color_options();
	printf("Enter color mode (0-10): ");
	config.color_type = get_int_input(0, 10);

	// Get ISP setting
	printf("\nEnable Image Signal Processing (ISP)?\n");
	printf("0: Disable\n");
	printf("1: Enable\n");
	printf("Enter choice (0-1): ");
	config.isp = get_int_input(0, 1);

	// Get Gamma setting
	printf("\nEnable Gamma Correction?\n");
	printf("0: Disable\n");
	printf("1: Enable\n");
	printf("Enter choice (0-1): ");
	config.gamma = get_int_input(0, 1);

	// Get scan mode
	printf("\nSelect Scan Mode:\n");
	printf("0: Stream Mode\n");
	printf("1: Scan Mode\n");
	printf("Enter choice (0-1): ");
	config.mode = get_int_input(0, 1);

	// Get number of images
	printf("\nNumber of images to capture (1-10): ");
	config.num_images = get_int_input(1, 10);

	// Apply settings
	printf("\nApplying custom settings...\n");
	set_color(config.color_type);
	set_isp(config.isp);
	set_gamma(config.gamma);

	// Start scanning
	printf("Press any key to start scanning...");
	_getch();
	start_capture();
}
// Default scan mode with preset settings including motor control
void default_scan_mode() {
	printf("\n=== Quick Scan Mode ===\n");

	// Set default settings for best color image with motor enabled
	config.color_type = 7;        // RGRB (Full Color)
	config.isp = 1;               // Enable ISP for better image quality
	config.gamma = 1;             // Enable gamma correction
	config.mode = 0;              // Stream mode
	config.num_images = 1;        // Capture one image
	//config.motor_enable = 1;      // Enable motor control
	//config.motor_direction = 0;
	//config.motor_step_freq = 3000; // Medium speed
	//config.motor_step_duty = 50;  // 50% duty cycle

	// Apply settings
	printf("Using optimal settings for color scanning with motor control...\n");
	set_color(config.color_type);
	set_isp(config.isp);
	set_gamma(config.gamma);

	// Start scanning
	printf("Press any key to start scanning...");
	_getch();
	start_capture();
}

// Clean up resources
void cleanup_resources() {
	printf("Cleaning up resources...\n");



	free(pixels);
	usb_config_thread.detach();
	cis_usb_deinit();

	printf("Cleanup complete.\n");
}

// Main function
int main() {
	printf("Scanner Control Application\n");
	printf("===========================\n");

	// Initialize scanner hardware
	initialize_scanner();

	int choice = 0;
	bool exit_program = false;

	// Main menu loop
	while (!exit_program) {
		display_main_menu();
		choice = get_int_input(1, 3);

		switch (choice) {
		case 1:
			default_scan_mode();
			break;

		case 2:
			custom_scan_mode();
			break;

		case 3:
			exit_program = true;
			printf("Exiting program...\n");
			break;
		}
	}

	// Clean up resources
	cleanup_resources();

	return 0;
}
