#define VIDEO_MEMORY 0xB8000
#define WHITE_ON_BLACK 0x0F

void print(const char* str) {
	unsigned char*video = (unsigned char*)VIDEO_MEMORY;

	while (*str) {
		*video++ = *str++; //character
		*video++ = WHITE_ON_BLACK; //colour atribute :P
		
	}
}

void kernel_main() {
	//clear screen first yay
//	unsigned char* video = (unsigned char*)VIDEO_MEMORY;
//	for (int i = 0; i < 80 * 25 *2; i++) {
//		video[1] = 0;
//	}


	while(1);

	//print("LanternOS kernel loaded!");
}
