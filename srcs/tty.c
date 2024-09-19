#include "kernel.h"

inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
	return fg | bg << 4;
}

static inline uint16_t vga_entry(uint8_t uc, uint8_t color) {
	return (uint16_t)uc | (uint16_t)color << 8;
}

void vga_init() {
	terminal_buffer = (uint16_t *)0xB8000;
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
	for (size_t i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++)
		terminal_buffer[i] = vga_entry(' ', terminal_color);
}

void terminal_initialize(void) {
	for (size_t i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++)
		terminal_buffer[i] =
			vga_entry(screen_buffer[kernel_screen][i], terminal_color);
	fb_move_cursor(screen_cursor[kernel_screen]);
}

void terminal_setcolor(uint8_t color) {
	terminal_color = color;
}

void terminal_putentryat(char c, uint8_t color, size_t index) {
	kmemshift(screen_buffer[kernel_screen], c, index, 2000);
	kvgashift(terminal_buffer, vga_entry(c, color), index, 2000);
}

bool is_hlt = false;

void terminal_putprompt() {
	for (size_t i = 0; i < PROMPT_LEN; i++)
		terminal_putentryat(PROMPT_STR[i], terminal_color,
							screen_cursor[kernel_screen]++);
}

void terminal_putchar(char c) {
	if (c == '\n') {
		screen_cursor[kernel_screen] += VGA_WIDTH;
		screen_cursor[kernel_screen] -=
			screen_cursor[kernel_screen] % VGA_WIDTH;
	}
	else
		terminal_putentryat(c, terminal_color, screen_cursor[kernel_screen]++);

	if (screen_cursor[kernel_screen] >= (VGA_WIDTH * VGA_HEIGHT)) {

		kmemmove(&screen_buffer[kernel_screen][VGA_WIDTH * 6],
				 &screen_buffer[kernel_screen][VGA_WIDTH * 7],
				 VGA_WIDTH * (VGA_HEIGHT - 7));
		kmemmove(&terminal_buffer[VGA_WIDTH * 6],
				 &terminal_buffer[VGA_WIDTH * 7],
				 VGA_WIDTH * (VGA_HEIGHT - 7) * sizeof(uint16_t));

		kmemset(&screen_buffer[kernel_screen][(VGA_HEIGHT - 1) * VGA_WIDTH],
				' ', VGA_WIDTH);
		kvgaset(&terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH],
				vga_entry(' ', terminal_color), VGA_WIDTH);
		screen_cursor[kernel_screen] = ((VGA_HEIGHT - 1) * VGA_WIDTH);
	}
	fb_move_cursor(screen_cursor[kernel_screen]);
}

void clear() {
	screen_cursor[kernel_screen] = 0;
	kmemset(&screen_buffer[kernel_screen][VGA_WIDTH * 6], 0,
			VGA_WIDTH * (VGA_HEIGHT - 6));
	screen_cursor[kernel_screen] = VGA_WIDTH * 6;
	terminal_putprompt();
	terminal_initialize();
}

void exec() {
	uint8_t *ptr = &input_buffer[VGA_WIDTH - 2];
	while (ptr > input_buffer && (!*ptr || *ptr == ' ')) {
		*ptr-- = '\0';
	}
	if (!kstrcmp((char *)input_buffer, "reboot"))
		reboot();
	if (!kstrcmp((char *)input_buffer, "halt")) {
		halt();
		kprint("HALT DONE\n");
		terminal_putprompt();
		fb_move_cursor(screen_cursor[kernel_screen]);
	}
	if (!kstrcmp((char *)input_buffer, "clear")) {
		clear();
	}
}

void handle_input(char c) {
	if (!c)
		return;
	input_cursor %= sizeof(input_buffer);
	if (c == '\n') {
		kmemset(input_buffer, 0, sizeof(input_buffer));
		size_t index =
			(screen_cursor[kernel_screen] -
			 (screen_cursor[kernel_screen] % VGA_WIDTH) - VGA_WIDTH) +
			PROMPT_LEN;
		kmemmove(input_buffer, &screen_buffer[kernel_screen][index], VGA_WIDTH);
		input_cursor = 0;
		is_cmd = true;
	}
}

void prompt(char c) {
	if (!c || c == '\n') {
		terminal_putchar('\n');
		handle_input(c);
		terminal_putprompt();
		fb_move_cursor(screen_cursor[kernel_screen]);
		return;
	}
	terminal_putchar(c);
	handle_input(c);
}

void terminal_write(char const *data, size_t size) {
	for (size_t i = 0; i < size; i++)
		terminal_putchar(data[i]);
}

void terminal_writestring(char const *data) {
	terminal_write(data, kstrlen(data));
}

void terminal_puthexa(unsigned long n) {
	if (n / 16)
		terminal_puthexa(n / 16);
	terminal_putchar("0123456789ABCDEF"[n % 16]);
}

void terminal_putnbr(uint32_t n) {
	if (n / 10)
		terminal_putnbr(n / 10);
	terminal_putchar((n % 10) + '0');
}

void switch_screen(int n) {
	if (n < 0)
		n = 9;
	static const uint8_t colors[][2] = {
		{VGA_COLOR_LIGHT_CYAN,	   VGA_COLOR_BLACK},
		{VGA_COLOR_LIGHT_BLUE,	   VGA_COLOR_BLACK},
		{VGA_COLOR_GREEN,		  VGA_COLOR_BLACK},
		{VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK},
		{VGA_COLOR_RED,			VGA_COLOR_BLACK},
		{VGA_COLOR_WHITE,		  VGA_COLOR_BLACK},
		{VGA_COLOR_LIGHT_GREY,	   VGA_COLOR_BLACK},
		{VGA_COLOR_LIGHT_BROWN,	VGA_COLOR_BLACK},
		{VGA_COLOR_LIGHT_BROWN,	VGA_COLOR_RED	 },
		{VGA_COLOR_BLACK,		  VGA_COLOR_WHITE}
	  };
	if (n == (int)kernel_screen)
		return;
	kernel_screen = n;
	terminal_setcolor(vga_entry_color(colors[n][0], colors[n][1]));
	terminal_initialize();
}

void delete_char(uint8_t code) {
	if (screen_cursor[kernel_screen] == (VGA_WIDTH * 6) + 2 + (code != 0x0E))
		return;

	if (screen_cursor[kernel_screen] % VGA_WIDTH == (2 - (code != 0x0E)))
		return;

	if (code == 0x0E) {
		--screen_cursor[kernel_screen];
	}
	// size_t len = VGA_WIDTH - (screen_cursor[kernel_screen] % VGA_WIDTH);
	size_t len = (VGA_WIDTH * VGA_HEIGHT) - screen_cursor[kernel_screen];
	kmemmove(&screen_buffer[kernel_screen][screen_cursor[kernel_screen]],
			 &screen_buffer[kernel_screen][screen_cursor[kernel_screen]] + 1,
			 len);
	kmemmove(&terminal_buffer[screen_cursor[kernel_screen]],
			 &terminal_buffer[screen_cursor[kernel_screen]] + 1,
			 len * sizeof(uint16_t));
	fb_move_cursor(screen_cursor[kernel_screen]);
}

void write_string_buffer(char *str) {
	for (size_t i = 0; i < kstrlen(str); i++) {
		char c = str[i];
		if (c == '\n') {
			screen_cursor[kernel_screen] += VGA_WIDTH;
			screen_cursor[kernel_screen] -=
				screen_cursor[kernel_screen] % VGA_WIDTH;
		}
		else
			// terminal_putentryat(c, terminal_color,
			// screen_cursor[kernel_screen]++);
			screen_buffer[kernel_screen][screen_cursor[kernel_screen]++] = c;
	}
}

void init_buffers(void) {
	char tmp[2] = {'\0', '\0'};
	for (size_t i = 0; i < 10; i++) {
		kernel_screen = (uint8_t)i;
		write_string_buffer("  _  _  ____  \n");
		write_string_buffer(" | || ||___ \\ \n");
		write_string_buffer(" | || |_ __) |\n");
		write_string_buffer(" |__   _/ __/ \n");
		write_string_buffer("    |_||_____|\n");
		write_string_buffer("tty: ");
		tmp[0] = i + '0';
		write_string_buffer(tmp);
		write_string_buffer("\n");
		write_string_buffer(PROMPT_STR);
	}
	kernel_screen = 0;
}
