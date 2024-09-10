/*
 */

#define F_CPU 16000000UL //16MHz
#include <xc.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include "picture.h"

#define SPI_DDR DDRB
#define SS PINB2
#define MOSI PINB3
#define SCK PINB5 //Datenübertragung synchron zu diesem Takt
#define Data_Command PIND2	
#define Reset PIND3		

#define BUTTON1_PRESSED !(PIND & (1 << PIND1)) //if set to 0 = Button pressed (Pull-Up Enable)
#define BUTTON2_PRESSED !(PINB & (1 << PINB1))

volatile uint16_t counter;

void SPISend8Bit(uint8_t data);
void SendCommandSeq(const uint16_t * data, uint32_t Anzahl);

ISR(TIMER1_COMPA_vect){
	counter++;	
}

void Waitms(const uint16_t msWait){
	static uint16_t aktTime, diff; /*aktTime ist Startwert, diff ist Differenz 
    aus Startwert und aktueller Zählerwert = wie viel Zeit vergangen ist*/
	uint16_t countertemp; //aktueller Zählerwert
	cli(); /*deactivates global interrupt bit to protect critical parts -> denn 
    könnte interrupt senden und unsere zwei separat gesendete Bytes unterbrechen*/
	aktTime = counter; //is set once
	sei();             
	do {
		cli();
		countertemp = counter;
		sei();
		diff = countertemp + ~aktTime + 1; //Zweierkomplement
	  } while (diff	< msWait); 	
}

void init_Timer1(){
	TCCR1B |= (1<<CS10) | (1<<WGM12); //CTC Mode & prescaler = 1
	TIMSK1 |= (1<<OCIE1A); //Timer1 Compare Match A Interrupt Enable				
	OCR1A = 15999; //Timerticks				
}

void SPI_init(){
	//set CS, MOSI and SCK as output (set to 1)
	SPI_DDR |= (1 << SS) | (1 << MOSI) | (1 << SCK); 

	//enable SPI Interrup, set as master, and clock to fosc/4 or 128 (durch SPI2X, SPR1, SPR0 = 0)
	SPCR = (1 << SPE) | (1 << MSTR); 
    //SPCR = SPI control register
}

void SPISend8Bit(uint8_t data){
	PORTB &= ~(1<<SS); //set SS to 0 (low) -> SPI activated (zuverlässiges Setzen des Slave-Geräts)
	SPDR = data; //load data into status register
	while(!(SPSR & (1 << SPIF))); //solange SPIF = 0 (keine Flag gesetzt), läuft Übertragung noch 
    //SPIF = SPI Interrupt Flag is set when a serial transfer is complete
	PORTB |= (1<<SS); //set SS to 1 (high) -> SPI deactivated
}

void SPISend16Bit(uint16_t data){ 
    uint8_t MSB;
    uint8_t LSB;
    
	MSB = (data >> 8) & 0xFF;     
	SPISend8Bit(MSB);
    
	LSB = data & 0xFF;			
	SPISend8Bit(LSB);
}

void SendCommandSeq(const uint16_t * data, uint32_t Anzahl){
	uint32_t index;
	uint8_t SendeByte;
    
	for (index=0; index<Anzahl; index++){
		PORTD |= (1<<Data_Command); //set Pin for Data/Command to 1 = Command-Mode	
 
		SendeByte = (data[index] >> 8) & 0xFF; //extracts MSB of command
		SPISend8Bit(SendeByte);
		SendeByte = data[index] & 0xFF; //extracts MSB of command
		SPISend8Bit(SendeByte);
        
		PORTD &= ~(1<<Data_Command); //set Pin for Data/Command to 0 = Data-Mode
	}
}

void Display_init(void) { 
	const uint16_t InitData[] ={
		0xFDFD, 0xFDFD,
		//pause
		0xEF00, 0xEE04, 0x1B04, 0xFEFE, 0xFEFE,
		0xEF90, 0x4A04, 0x7F3F, 0xEE04, 0x4306, //0x7F3F for 16-bit color mode
		//pause
		0xEF90, 0x0983, 0x0800, 0x0BAF, 0x0A00,
		0x0500, 0x0600, 0x0700, 0xEF00, 0xEE0C,
		0xEF90, 0x0080, 0xEFB0, 0x4902, 0xEF00,
		0x7F01, 0xE181, 0xE202, 0xE276, 0xE183,
		0x8001, 0xEF90, 0x0000,
		//pause
		0xEF08,	0x1805,	//landscape mode
        0x1283, 0x1500,	0x1300, 0x16AF 	//full display 132x176 Pixel
	};
    //Reset-Routine (Reversed Engineering)
	Waitms(300);
	PORTD &= ~(1<<Reset); //Reset-Eingang des Displays auf Low => Beginn Hardware-Reset
	Waitms(75);
	PORTB |= (1<<SS); //SSEL auf High
	Waitms(75);
	PORTD |= (1<<Data_Command);	//Data/Command auf High
	Waitms(75);
	PORTD |= (1<<Reset); //Reset-Eingang des Displays auf High => Ende Hardware Reset
	Waitms(75);
	SendCommandSeq(&InitData[0], 2);
	Waitms(75);
	SendCommandSeq(&InitData[2], 10);
	Waitms(75);
	SendCommandSeq(&InitData[12], 23);
	Waitms(75);
	SendCommandSeq(&InitData[35], 6);
}

int main(void){ 
	DDRD |= (1<<Data_Command)|(1<<Reset); //set as output in controller 
	init_Timer1();
	SPI_init();
	Display_init();

    //data for green BG
    for(uint16_t i = 0; i < 132*176; i++){
        SPISend8Bit(0x07); 
        SPISend8Bit(0xE0);
    }
    
    //commands for picture
    static uint16_t WindowData[] = {
        0xEF08,	0x1805,	//landscape-mode
        0x1268, 0x151C,	0x1312, 0x169D
    };
    SendCommandSeq(WindowData, 6);
    
    /* display red rectangle:
    for(uint16_t i = 0; i < 140*76; i++){
        SPISend8Bit(0xF8);
        SPISend8Bit(0x00);
    }
    */
    
    static volatile uint16_t lastColor;
    static volatile uint16_t currentColor;
    static volatile uint16_t possibleRepitions;
    
    for(uint16_t i = 0; i < 2904; i++){
        lastColor = Bild1[i];
        currentColor = Bild1[i+1];
        possibleRepitions = Bild1[i+2];
        
        if(lastColor == currentColor) {
            SPISend16Bit(lastColor);
            for(uint16_t j = 0; j < possibleRepitions + 1; j++) { //will be displayed a second time + additional times if applicable
                SPISend16Bit(lastColor);
            }
            i += 2; 
        } else {
            SPISend16Bit(lastColor); //display color only once
        }
    }
            
	while(1){
    }
}

