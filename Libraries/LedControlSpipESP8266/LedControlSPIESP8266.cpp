/*
  LedControl.h - A library for controling Leds with a MAX7219/MAX7221
  Copyright (c) 2007 Eberhard Fahle
  
  Adapted for ESP8266 by Jean-Philippe CIVADE (jp.civade@labsud.org)

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "LedControlSPIESP8266.h"


//the opcodes for the MAX7221 and MAX7219
#define OP_NOOP   0
#define OP_DIGIT0 1
#define OP_DIGIT1 2
#define OP_DIGIT2 3
#define OP_DIGIT3 4
#define OP_DIGIT4 5
#define OP_DIGIT5 6
#define OP_DIGIT6 7
#define OP_DIGIT7 8
#define OP_DECODEMODE  9
#define OP_INTENSITY   10
#define OP_SCANLIMIT   11
#define OP_SHUTDOWN    12
#define OP_DISPLAYTEST 15

LedControl::LedControl(int csPin, int numDevices) {
	SPI.begin();
	SPI.setFrequency(8000000);
    SPI_CS=csPin;
    // is it an hardware limit?
    if(numDevices<=0 || numDevices>8 ) {
    	numDevices=8;
    }
    // set number of devices
    maxDevices=numDevices;
    // Assign Chip Select Pin 
    pinMode(SPI_CS,OUTPUT);
    digitalWrite(SPI_CS,HIGH);
    for(int i=0;i<(8*maxDevices);i++) {
    	status[i]=0x00;
    }
    for(int i=0;i<maxDevices;i++) {
    	spiTransfer(i,OP_DISPLAYTEST,0);
    	//scanlimit is set to max on startup
    	setScanLimit(i,7);
    	//decode is done in source
    	spiTransfer(i,OP_DECODEMODE,0);
    	clearDisplay(i);
    	// we go into shutdown-mode on startup
    	shutdown(i,true);
    }
}

int LedControl::getDeviceCount() {
    return maxDevices;
}

void LedControl::shutdown(int addr, bool b) {
    if(addr<0 || addr>=maxDevices)
	return;
    if(b) {
    	spiTransfer(addr, OP_SHUTDOWN,0);
    }
    else {
    	spiTransfer(addr, OP_SHUTDOWN,1);
	}
}
	
void LedControl::setScanLimit(int addr, int limit) {
    if(addr<0 || addr>=maxDevices) {
    	return;
    }
    if(limit>=0 || limit<8) {
    	spiTransfer(addr, OP_SCANLIMIT,limit);
    }
}

void LedControl::setIntensity(int addr, int intensity) {
    if(addr<0 || addr>=maxDevices) {
    	return;
    }
    if(intensity>=0 || intensity<16) {
    	spiTransfer(addr, OP_INTENSITY,intensity);
    }
}

void LedControl::clearDisplay(int addr) {
    int offset;

    if(addr<0 || addr>=maxDevices) {
    	return;
    }
    offset=addr*8;
    for(int i=0;i<8;i++) {
    	status[offset+i]=0;
    	spiTransfer(addr, i+1,status[offset+i]);
    }
}

void LedControl::setLed(int addr, int row, int column, boolean state) {
    int offset;
    byte val=0x00;

    if(addr<0 || addr>=maxDevices) {
    	return;
    }
    if(row<0 || row>7 || column<0 || column>7) {
    	return;
    }
    offset=addr*8;
    val=B10000000 >> column;
    if(state) {
    	status[offset+row]=status[offset+row]|val;
    }
    else {
    	val=~val;
		status[offset+row]=status[offset+row]&val;
    }
    spiTransfer(addr, row+1,status[offset+row]);
}
	
void LedControl::setRow(int addr, int row, byte value) {
    int offset;
    if(addr<0 || addr>=maxDevices) {
    	return;
    }
    if(row<0 || row>7) {
    	return;
    }
    offset=addr*8;
    status[offset+row]=value;
    spiTransfer(addr, row+1,status[offset+row]);
}
    
void LedControl::setColumn(int addr, int col, byte value) {
    byte val;

    if(addr<0 || addr>=maxDevices) {
    	return;
    }
    if(col<0 || col>7) {
    	return;
    }
    for(int row=0;row<8;row++) {
    	val=value >> (7-row);
    	val=val & 0x01;
    	setLed(addr,row,col,val);
    }
}

void LedControl::setDigit(int addr, int digit, byte value, boolean dp) {
    int offset;
    byte v;

    if(addr<0 || addr>=maxDevices) {
    	return;
    	}
    if(digit<0 || digit>7 || value>15) {
    	return;
    	
    }
    offset=addr*8;
    v=charTable[value];
    if(dp) {
    	v|=B10000000;
    }
    status[offset+digit]=v;
    spiTransfer(addr, digit+1,v);
    
}

void LedControl::setChar(int addr, int digit, char value, boolean dp) {
    int offset;
    byte index,v;

    if(addr<0 || addr>=maxDevices) {
    	return;
    }
    if(digit<0 || digit>7) {
    	return;
    }
    offset=addr*8;
    index=(byte)value;
    if(index >127) {
    	//nothing define we use the space char
    	value=32;
    }
    v=charTable[index];
    if(dp) {
    	v|=B10000000;
    }
    status[offset+digit]=v;
    spiTransfer(addr, digit+1,v);
}

void LedControl::spiTransfer(int addr, volatile byte opcode, volatile byte data) {
    //Create an array with the data to shift out
    int offset=addr*2;
    int maxbytes=maxDevices*2;

    for(int n=0;n<maxbytes;n++) {
    	spidata[n]=0x00;
    }
    // put our device data into the array
    spidata[offset+1]=opcode;
    spidata[offset]=data;
    // enable the software chip select line 
    digitalWrite(SPI_CS,LOW);
    // Now shift out the data 
    for(int n=maxbytes;n>0;n--){
 		SPI.transfer(spidata[n-1]);
		}
 	//latch the data onto the display
 	digitalWrite(SPI_CS,HIGH);
}    


