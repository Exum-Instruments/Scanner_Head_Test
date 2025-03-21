Usage:
  Executable file located in Bin folder.
  Run using: cis_example -flags
    Example: cis_example -c 5 -i 1 -g 1 -m 0 -n 5 
	-Color: RGB
	-ISP: On
	-Gamma: On
	-Mode: Streaming 
	-Number of Images: 5

Flags:
  -c <color_type>: Choose a color (Default: 0 - Off)
    Color Index Options:
      0: Off
      1: Red
      2: Green
      3: Blue
      4: IR
      5: RGB
      6: RGBGRW
      7: RGBG
      8: RGBH (unstable)
      9: Grayscale
      10: Black & White

  -i <0|1>: Enable or disable ISP (Default: 0 - Disabled)
    0: Disable
    1: Enable

  -g <0|1>: Enable or disable Gamma (Default: 0 - Disabled)
    0: Disable
    1: Enable

  -m <0|1>: Set mode (Default: 0 - Stream Mode)
    0: Stream Mode
    1: Scan Mode

  -n <num_images>: Number of images to capture (Default: 5)

  -h or --help: Display this help message

Notes: 
- This example is based on the cis_img DLL file. For access to the DLL source code, please contact Eric at: eric@csensor.com
- CPU performance or Windows speed may cause data loss in the module when using libusb.
- Developed on Visual Studio 2022 Version 17.7.2
