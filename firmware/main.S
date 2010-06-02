;
; Copyright (c) 2010 Peter Zotov <whitequark@whitequark.org>
;
; Permission is hereby granted, free of charge, to any person obtaining
; a copy of this software and associated documentation files (the
; "Software"), to deal in the Software without restriction, including
; without limitation the rights to use, copy, modify, merge, publish,
; distribute, sublicense, and/or sell copies of the Software, and to
; permit persons to whom the Software is furnished to do so, subject to
; the following conditions:
;
; The above copyright notice and this permission notice shall be included
; in all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
; MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
; IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
; CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
; TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
; SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include <avr/io.h>
#ifndef USER_UBRR
#include "setbaud.h"
#endif

#define EEPROM_BYTES (1 << E_EEPROM_BYTES)
#define PAGE_WORDS   (1 << E_PAGE_WORDS)
#define FLASH_PAGES  (1 << E_FLASH_PAGES)

#define IO(reg) _SFR_IO_ADDR(reg)

; check tail of the file
#ifdef EEPROM
#  define SIGNATURE_LEN 9
#else
#  define SIGNATURE_LEN 8
#endif

.org BOOT_BYTE

.globl entry
entry:
	cli

#if RAMEND > 255
	ldi	r16, hi8(RAMEND)
	out	IO(SPH), r16
#endif
	ldi	r16, lo8(RAMEND)
	out	IO(SPL), r16

; set up UART: 8n1
#ifndef USER_UBRR
; use calculated by util/setbaud.h
#if UBRRH_VALUE > 0
	ldi	r16, UBRRH_VALUE
	out	IO(UBRRH), r16
#endif
	ldi	r16, UBRRL_VALUE
	out	IO(UBRRL), r16
#else
; use provided
#if (USER_UBRR >> 8) > 0
	ldi	r16, hi8(USER_UBRR)
	out	IO(UBRRH), r16
#endif
	ldi	r16, lo8(USER_UBRR)
	out	IO(UBRRL), r16
#endif

#ifdef USE_2X
	ldi	r16, _BV(U2X)
	out	IO(UCSRA), r16
#endif

	ldi	r16, _BV(RXEN) | _BV(TXEN)
	out	IO(UCSRB), r16

	ldi	r16, _BV(URSEL) | _BV(UCSZ0) | _BV(UCSZ1)
	out	IO(UCSRC), r16

	rjmp	the_loop

cmd_quit:
	ldi	r16, _BV(RWWSRE) | _BV(SPMEN)
	rcall	do_spm

	clr	ZH
	clr	ZL
	ijmp

; send signature and uC info
cmd_signature:
	ldi	r16, SIGNATURE_LEN
	ldi	ZH, hi8(signature)
	ldi	ZL, lo8(signature)

0:
	lpm	r20, Z+
	rcall	send
	dec	r16
	brne	0b

;	rjmp the_loop

the_loop:
	rcall	recv

	cpi	r20, 's'
	breq	cmd_signature

	cpi	r20, 'w'
	breq	cmd_write_flash

	cpi	r20, 'r'
	breq	cmd_read_flash

	cpi	r20, 'W'
	breq	cmd_write_eeprom

	cpi	r20, 'R'
	breq	cmd_read_eeprom

	cpi	r20, 'q'
	breq	cmd_quit

	ldi	r20, 'E'
	rcall	send
	rjmp	the_loop

recv_page:
; receive and convert page number => ZH:r5
	rcall	recv
	ldi	r16, PAGE_WORDS*2
	mul	r20, r16 ; no lsl rX, n instruction!

	mov	ZH, r1
	mov	r5, r0

	rcall	recv
	ldi	r16, PAGE_WORDS*2
	mul	r20, r16

	add	ZH, r0

	ret

cmd_write_flash:
	rcall recv_page

; erase page
	mov	ZL, r5

	ldi	r16, _BV(PGERS) | _BV(SPMEN)
	rcall	do_spm

	ldi	r20, '.'
	rcall	send

; fill internal buffer
	ldi	ZL, 0
0:	rcall	recv
	mov	r0, r20
	rcall	recv
	mov	r1, r20

	ldi	r16, _BV(SPMEN)
	rcall	do_spm

	inc	ZL
	cpi	ZL, PAGE_WORDS
	brne	0b

; do the programming itself
	mov	ZL, r5

	ldi	r16, _BV(PGWRT) | _BV(SPMEN)
	rcall	do_spm

	ldi	r20, '.'
	rcall	send

	rjmp the_loop

cmd_read_flash:
	rcall	recv_page
	mov	ZL, r5

	ldi	r16, PAGE_WORDS*2
0:	lpm	r20, Z+
	rcall	send
	dec	r16
	brne	0b

	rjmp	the_loop

cmd_read_eeprom:
	clr	r27
	clr	r26
	ldi	r25, hi8(EEPROM_BYTES)
	ldi	r24, lo8(EEPROM_BYTES)

0:	out	IO(EEARH), r27
	out	IO(EEARL), r26

	sbi	IO(EECR), EERE
	in	r20, IO(EEDR)
	rcall	send

	adiw	r26, 1
	sbiw	r24, 1
	brne	0b

	rjmp	the_loop

cmd_write_eeprom:
	rcall	recv
	out	IO(EEARL), r20
	rcall	recv
	out	IO(EEARH), r20

	rcall	recv
	out	IO(EEDR), r20

	sbi	IO(EECR), EEMWE
	sbi	IO(EECR), EEWE

0:	sbic	IO(EECR), EEWE
	rjmp	0b

	ldi	r20, '.'
	rcall	send

	rjmp	the_loop

recv:
	sbis	IO(UCSRA), RXC
	rjmp	recv
	in	r20, IO(UDR)
	ret

send:
	sbis	IO(UCSRA), UDRE
	rjmp	send
	out	IO(UDR), r20
	ret

do_spm:
	out	IO(SPMCR), r16
	spm
0:	in	r16, IO(SPMCR)
	sbrc	r16, SPMEN
	rjmp	0b
	ret

signature:
	.byte 'V', 'u', 'X'
#ifdef EEPROM
	.byte 'e', E_EEPROM_BYTES
#define TYPE_BYTES 'e'+E_EEPROM_BYTES
#else
	.byte 'f'
#define TYPE_BYTES 'f'
#endif
	.byte PAGE_WORDS, E_FLASH_PAGES, BOOT_PAGES
	.byte ('V'+'u'+'X'+TYPE_BYTES+PAGE_WORDS+E_FLASH_PAGES+BOOT_PAGES) ; "CRC"

