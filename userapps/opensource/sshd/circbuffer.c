/*
 * Dropbear SSH
 * 
 * Copyright (c) 2002-2004 Matt Johnston
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

#include "includes.h"
#include "dbutil.h"
#include "circbuffer.h"

#define MAX_CBUF_SIZE 100000000

circbuffer * cbuf_new(unsigned int size) {

	circbuffer *cbuf = NULL;

	if (size > MAX_CBUF_SIZE) {
		dropbear_exit("bad cbuf size");
	}

	cbuf = (circbuffer*)m_malloc(sizeof(circbuffer));
	cbuf->data = (unsigned char*)m_malloc(size);
	cbuf->used = 0;
	cbuf->readpos = 0;
	cbuf->writepos = 0;
	cbuf->size = size;

	return cbuf;
}

void cbuf_free(circbuffer * cbuf) {

	m_free(cbuf->data);
	m_free(cbuf);
}

unsigned int cbuf_getused(circbuffer * cbuf) {

	return cbuf->used;

}

unsigned int cbuf_getavail(circbuffer * cbuf) {

	return cbuf->size - cbuf->used;

}

unsigned int cbuf_readlen(circbuffer *cbuf) {

	assert(((2*cbuf->size)+cbuf->writepos-cbuf->readpos)%cbuf->size == cbuf->used%cbuf->size);
	assert(((2*cbuf->size)+cbuf->readpos-cbuf->writepos)%cbuf->size == (cbuf->size-cbuf->used)%cbuf->size);

	if (cbuf->used == 0) {
		TRACE(("cbuf_readlen: unused buffer"))
		return 0;
	}

	if (cbuf->readpos < cbuf->writepos) {
		return cbuf->writepos - cbuf->readpos;
	}

	return cbuf->size - cbuf->readpos;
}

unsigned int cbuf_writelen(circbuffer *cbuf) {

	assert(cbuf->used <= cbuf->size);
	assert(((2*cbuf->size)+cbuf->writepos-cbuf->readpos)%cbuf->size == cbuf->used%cbuf->size);
	assert(((2*cbuf->size)+cbuf->readpos-cbuf->writepos)%cbuf->size == (cbuf->size-cbuf->used)%cbuf->size);

	if (cbuf->used == cbuf->size) {
		TRACE(("cbuf_writelen: full buffer"))
		return 0; /* full */
	}
	
	if (cbuf->writepos < cbuf->readpos) {
		return cbuf->readpos - cbuf->writepos;
	}

	return cbuf->size - cbuf->writepos;
}

unsigned char* cbuf_readptr(circbuffer *cbuf, unsigned int len) {
	if (len > cbuf_readlen(cbuf)) {
		dropbear_exit("bad cbuf read");
	}

	return &cbuf->data[cbuf->readpos];
}

unsigned char* cbuf_writeptr(circbuffer *cbuf, unsigned int len) {

	if (len > cbuf_writelen(cbuf)) {
		dropbear_exit("bad cbuf write");
	}

	return &cbuf->data[cbuf->writepos];
}

void cbuf_incrwrite(circbuffer *cbuf, unsigned int len) {
	if (len > cbuf_writelen(cbuf)) {
		dropbear_exit("bad cbuf write");
	}

	cbuf->used += len;
	assert(cbuf->used <= cbuf->size);
	cbuf->writepos = (cbuf->writepos + len) % cbuf->size;
}


void cbuf_incrread(circbuffer *cbuf, unsigned int len) {
	if (len > cbuf_readlen(cbuf)) {
		dropbear_exit("bad cbuf read");
	}

	assert(cbuf->used >= len);
	cbuf->used -= len;
	cbuf->readpos = (cbuf->readpos + len) % cbuf->size;
}
