// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 *   Copyright (C) 2013 Sheep Sun <sunxiaoyang2003@gmail.com>              *
 *   Copyright (C) 2013 Paul Fertser <fercerpav@gmail.com>                 *
 *   Copyright (C) 2012 by Creative Product Design, marc @ cpdesign.com.au *
 ***************************************************************************/

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define LOG_ERROR(...)		do {					\
		fprintf(stderr, __VA_ARGS__);				\
		fputc('\n', stderr);					\
	} while (0)
#define LOG_WARNING(...)	LOG_ERROR(__VA_ARGS__)

#define ERROR_OK	(-1)
#define ERROR_FAIL	(-2)
#define ERROR_JTAG_INIT_FAILED	ERROR_FAIL

#define BIT(x)                      ( 1 <<(x) )
#define REG_AHB_DSP_JTAG_CTRL       ( 0x20900280 )
#define BIT_CEVA_SW_JTAG_ENA        ( BIT(8) )
#define BIT_STDI                    ( BIT(4) ) //oh fuck
#define BIT_STCK                    ( BIT(3) )
#define BIT_STMS                    ( BIT(2) )
#define BIT_STDO                    ( BIT(1) )
#define BIT_STRTCK                  ( BIT(0) )

volatile uint32_t *jtagreg;

static int		devmem_fd;

static void *devm_map(unsigned long addr, int len)
{
	off_t offset;
	void *map_base; 

	if ((devmem_fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) {
		LOG_ERROR("cannot open '/dev/mem'\n");
		goto err_open;
	}
	LOG_WARNING("/dev/mem opened.\n");

	/*
	 * Map it
	 */

	/* offset for mmap() must be page aligned */
	offset = addr & ~(sysconf(_SC_PAGE_SIZE) - 1);

	map_base = mmap(NULL, len + addr - offset, PROT_READ | PROT_WRITE,
			MAP_SHARED, devmem_fd, offset);
	if (map_base == MAP_FAILED) {
		LOG_ERROR("mmap failed\n");
		goto err_mmap;
	}
	LOG_WARNING("Memory mapped at address %p.\n", map_base); 

	return map_base + addr - offset;

err_mmap:
	close(devmem_fd);

err_open:
	return NULL;
}

static void devm_unmap(void *virt_addr, int len)
{
	unsigned long addr;

	if (devmem_fd == -1) {
		LOG_ERROR("'/dev/mem' is closed\n");
		return;
	}

	/* page align */
	addr = (((unsigned long)virt_addr) & ~(sysconf(_SC_PAGE_SIZE) - 1));
	munmap((void *)addr, len + (unsigned long)virt_addr - addr);
	close(devmem_fd);
}

static void djtag_enable(int en)
{
    uint32_t reg;

    reg = (*jtagreg);
    reg &= ~BIT_CEVA_SW_JTAG_ENA;
    reg |= (en ? BIT_CEVA_SW_JTAG_ENA : 0);
    (*jtagreg) = reg;
}

static void djtag_set_tck(int tck)
{
    uint32_t reg;

    if(tck)
    {
        reg = (*jtagreg);
        reg |= BIT_STCK;
        (*jtagreg) = reg;
        while(((*jtagreg) & BIT_STRTCK) == 0);
    } 
    else
    {
        reg = (*jtagreg);
        reg &= ~BIT_STCK;
        (*jtagreg) = reg;
        while((*jtagreg) & BIT_STRTCK);
    }
}

static void djtag_set_tdi(int tdi)
{
    uint32_t reg;

    reg = (*jtagreg);
    reg &= ~BIT_STDI;
    reg |= (tdi ? BIT_STDI : 0);
    (*jtagreg) = reg;
}

static void djtag_set_tms(int tms)
{
    uint32_t reg;

    reg = (*jtagreg);
    reg &= ~BIT_STMS;
    reg |= (tms ? BIT_STMS : 0);
    (*jtagreg) = reg;
}

static void djtag_set(int tck, int tms, int tdi)
{
	static int last_tck;
	static int last_tms;
	static int last_tdi;

	static int first_time;
	size_t bytes_written;

	if (!first_time) {
		last_tck = !tck;
		last_tms = !tms;
		last_tdi = !tdi;
		first_time = 1;
	}

	if (tdi != last_tdi) {
		djtag_set_tdi(tdi);
	}

	if (tms != last_tms) {
		djtag_set_tms(tms);
	}

	/* write clk last */
	if (tck != last_tck) {
		djtag_set_tck(tck);
	}

	last_tdi = tdi;
	last_tms = tms;
	last_tck = tck;
}

static int djtag_get()
{
    return ((*jtagreg) & BIT_STDO ? '1' : '0');
}

static int djtag_map()
{
	void *virt_addr;

	virt_addr = devm_map(REG_AHB_DSP_JTAG_CTRL, 4);

	if (virt_addr == NULL) {
		LOG_ERROR("djtag register map failed");
		return ERROR_FAIL;
	}
    jtagreg = (uint32_t *)virt_addr;

	return 0;
}

static void djtag_unmap()
{
    if(jtagreg)
    {
        devm_unmap(jtagreg, 4);
    }
	jtagreg = NULL;
}

static void process_remote_protocol(void)
{
	int c;
	while (1) {
		c = getchar();
		if (c == EOF || c == 'Q') /* Quit */
			break;
		else if (c == 'b' || c == 'B') /* Blink */
			continue;
		else if (c >= 'r' && c <= 'r' + 3) /* Reset */
			continue;
		else if (c >= '0' && c <= '0' + 7) {/* Write */
			char d = c - '0';
			djtag_set(!!(d & 4),
					!!(d & 2),
					(d & 1));
		} else if (c == 'R')
			putchar(djtag_get());
		else
			LOG_ERROR("Unknown command '%c' received", c);
	}
}

int main(int argc, char *argv[])
{
	int ret;

	LOG_WARNING("SC8810 DJTAG remote_bitbang JTAG driver\n");

	ret = djtag_map();
	if (ret < 0)
		goto out_error;
	djtag_enable(1);

	setvbuf(stdout, NULL, _IONBF, 0);
	process_remote_protocol();

	djtag_enable(0);
	djtag_unmap();
	return 0;
out_error:
	djtag_unmap();
	return ERROR_JTAG_INIT_FAILED;
}
