#include "pch.h"
#include "cis_core.h"
#include "cis_img.h"
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
int buf_h1 = 960;
int color_type = 0;
int w1 = 1280;
int h1 = 960;

// Configuration structure
struct CISConfig {
	int color_type;  // Color mode
	int isp;         // Image Signal Processing
	int gamma;       // Gamma correction
	int mode;        // Scan mode
	int num_images;  // Number of images to capture
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

// Capture an image from the scanner
bool captureImage() {
	if (config.num_images <= 0) {
		return false;
	}

	if (checkFrameData()) {
		if (color_type > 0 && color_type < 5) { // Save cropped raw image
			unsigned char* pRaw = (unsigned char*)malloc(w1 * 720);
			int i = getRawImage(pRaw, 0, 0, (short)w1, 720);
			if (i == 720) {
				Raw2Bmp(pixels, pRaw, (short)(w1 - 8), (short)i, (short)color_type);
				swap_rgb_to_bgr(pixels, w1 - 8, 720);
				save_image(w1 - 8, 720);
				config.num_images--;
			}
			free(pRaw);
		}
		else { // Save full image
			getImage(pixels, color_type);
			swap_rgb_to_bgr(pixels, w1 - 8, h1);
			save_image(w1 - 8, h1);
			config.num_images--;
		}

		return true;
	}

	if (done) {
		return SOURCE_REMOVE;
	}

	return SOURCE_CONTINUE;
}

// Initialize and start the scanning process
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
		h1 = 960;
	}

	cis_init(w1, h1, 1);
	if (color_type == 7) // RGBG
	{
		h1 = h1 >> 1;
	}
	else if (color_type == 8) //RGBH
	{
		h1 = h1 / 3;
	}

	setScanMode(config.mode);
	cis_usb_start();

	printf("Scanning in progress...\n");



	while (1) {
		Sleep(200);
		if (captureImage()) {
			printf("Scan complete!\n");
			break;
		}
	}


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
	pixels = (unsigned char*)malloc(1288 * 960 * 3);

	printf("Scanner initialized successfully.\n");
}

// Clean up resources
void cleanup_resources() {
	printf("Cleaning up resources...\n");

	free(pixels);
	usb_config_thread.detach();
	cis_usb_deinit();

	printf("Cleanup complete.\n");
}

// Custom scan mode with user-defined settings
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

// Default scan mode with preset settings
void default_scan_mode() {
	printf("\n=== Quick Scan Mode ===\n");

	// Set default settings for best color image
	config.color_type = 5;  // RGB (Full Color)
	config.isp = 1;         // Enable ISP for better image quality
	config.gamma = 1;       // Enable gamma correction
	config.mode = 0;        // Stream mode
	config.num_images = 1;  // Capture one image

	// Apply settings
	printf("Using optimal settings for color scanning...\n");
	set_color(config.color_type);
	set_isp(config.isp);
	set_gamma(config.gamma);

	// Start scanning
	printf("Press any key to start scanning...");
	_getch();
	start_capture();
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
