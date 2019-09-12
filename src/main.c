#define F_CPU 1000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

//global variable setup
volatile uint16_t timeCounter = 0; // counter for normal and config mode
volatile uint16_t timeCounter1 = 0; //counter for red light camera
volatile uint16_t timeCounter2 = 0; //counter for task 3 pwm
volatile uint16_t light = 1; //indicator for current light colour
volatile uint8_t speed = 0; //speed for task 3
volatile uint8_t period = 1; //time between light changing in normal mode
volatile uint8_t modeChange = 0; //flag for changing to config mode
volatile uint8_t configMode = 0; //flag for entering config mode
volatile uint8_t configCycle = 1; //variable to determine the blinking cycle during config mode
volatile uint8_t configCount = 0; //variable to keep track of current place in blinking cycle during config mode
volatile uint8_t configPeriod = 1; //temp variable so program responds correctly if pot changes during blink cycle 
volatile uint8_t redLightCamera = 0; //flag to enter redLightCameraMode
volatile uint8_t redLightCount = 0; //current count of number of cars who crossed the red light
volatile uint8_t redLightTimer = 0; //counter for redlight blinking cycle
volatile uint8_t temp = 0; //flag so polling in task 4 only executes once per press

//timer interrupt, executes every 2ms
ISR(TIMER2_COMP_vect)
{
	timeCounter++;
	timeCounter1++;
	timeCounter2++;
}

//task 3
//zero overflow counter when LB1 is crossed
ISR(INT0_vect)
{
	timeCounter2 = 0;
}
//take timestamp to calculate speed and set pwm when LB2 is crossed
//speed = (20*3.6)/(timeCounter2/500) using constant 36000 for easier calculations
ISR(INT1_vect)
{
	speed = 36000/timeCounter2;
	if (speed >= 100) {speed = 100;}
	OCR1A = speed*2.55;
}
//timer1 for pwm
ISR(TIMER1_OVF_vect){}

//task 1, normal mode
//cycle between green/yellow/red each time (light (mod 3) overflows light on 3, offset by 3 due to pb1/2 being used for pwm)
//led4 is to be on permanently during normal mode
void normalMode(){
	light++;
	PORTB &= ~(1 << (light%3 + 3));
	PORTB |= (1 << ((light+1)%3 + 3));
	PORTB |= (1 << ((light+2)%3 + 3));
	PORTD &= ~(1 << 5);
	timeCounter = 0;
}

//task 2, config mode
void configurationMode(){
	//start single conversion, read value
	ADCSRA |= (1<<ADSC);
	while (ADCSRA & (1<<ADSC)){}
	uint16_t adcInput = ADC;
	//evenly divide adcInput into 4 ranges, with outputs being 1, 2, 3 and 4. (Integer division floors)
	period = (adcInput/256)+1;
	//algorithm for blinking, configcycle of 1 or 6 determines if current blink should last 0.5 or 3 seconds, this is decieded by the current period
	if (timeCounter >= 250*configCycle)
	{
		//store the period, so it does not change during a blinking cycle. 
		if (configCount == 0) {configPeriod = period;}
		//flip led4 on/off with every iteration of the loop
		PORTD ^= (1<<5);
		configCount++;
		//if light has blinked on and off for the same number of times as the period, set next loop's wait time to 3 seconds
		if (configCount >= (2*configPeriod - 1)) {configCycle = 6;}
		//reset all variables after 3 second wait so cycle starts again
		if (configCount >= (2*configPeriod)) {configCycle = 1; configCount = 0;}
		timeCounter = 0;
	}
}

//task 4, red light camera
void redLightCameraMode(){
	//flip pb0 on/off every for iteration of the loop
	PORTB ^= (1<<0);
	//increment pwm redlight counter by 1% for each additional button press
	if (redLightCount > 100) {redLightCount = 100;}
	OCR1B = redLightCount*2.55;
	//decrement and reset varialbe after cycle finishes
	redLightTimer--;
	if (redLightTimer == 0) {redLightCamera = 0;}
	timeCounter1 = 0;
}

int main(void)
{
	//enable timer1 and timer2
	TIMSK |= (1<<OCIE2 | 1<<TOIE1);
	//timer2 for timing tasks, prescaler set at 8, timer overflows every 2ms
	TCCR2 |= (1<<CS21 | 1<<WGM21);
	OCR2 = 255;
	//timer 1 for hardware pwm, prescaler set at 64, pwm period is 255
	TCCR1A |= (1<<WGM10 | 1<<COM1A1 | 1<<COM1B1);
	TCCR1B |= (1<<WGM12 | 1<<CS12);
	TCNT1 = 0xFFFF - 3906;
	
	//enable hardware interrupt on pd2 and pd3
	GICR = (1<<INT0 | 1<<INT1);
	//set both to be falling edge (active low)
	MCUCR = (1<<ISC01 | 1<<ISC11);
	
	//adc setup, using single conversion mode
	ADMUX = 0;
	ADCSRA = 0;
	ADCSRA |= (1<<ADEN | 1<<ADPS1 | 1<<ADPS0);
	
	//enable global interrupts
	sei();

	//io setup
	DDRB |= (1<<DDB0 | 1<<DDB1 | 1<<DDB2 | 1<<DDB3 | 1<<DDB4 | 1<<DDB5);
	PORTB |= ( 1<<PB0 | 1<<PB1 | 1<<PB2 | 1<<PB3 |1 <<PB4 | 1<<PB5);
	DDRD &= ~(1<<DDD0 | 1<<DDD1 | 1<<DDD2 | 1<<DDD3);
	DDRD |= (1<<DDD5);
	PORTD |= (1<<PD5);
	
	while (1)
	{
		_delay_us(1); //delay such that program doesn't skip polling
		//polls sw7, if its pressed during redlight, flag redlight camera, increment count, set timer flag for flashing 
		//(last parameter prevents condition running more than once per click)
		if (((PIND & (1<<DDD1)) == 0) && ((light%3 + 2) == 4) && (redLightTimer == 0))
		{redLightCamera = 1; redLightCount++; redLightTimer = 4;}
		//if redlight camera flag is on, execute loop every 0.5s until timer flag runs out
		if ((redLightCamera == 1) && (timeCounter1 >= 250) && (redLightTimer > 0))
		{redLightCameraMode();}
		
		//polling sw0, flip modeChange flag between 0/1 for each press, temp prevents more than one change per click
		if (((PIND & (1<<DDD0)) == 0) && (temp == 0))
		{modeChange++; modeChange=modeChange%2; temp = 1;}
		if (((PIND & (1<<DDD0)) != 0) && (temp == 1))
		{temp = 0;}
		
		//enter configuration mode on redlight if modeChange flag is on, else stay in normal mode
		if ((modeChange == 1) && (light%3 + 2) == 4)
		{configMode = 1;}
		else if (modeChange == 0)
		{configMode = 0; configCycle = 1; configCount = 0;}

		//during normal mode, execute loop every x second, where x is the period, x is 1 by default
		if ((configMode == 0) && (timeCounter >= 500*period))
		{normalMode();}
		else if (configMode == 1)
		{configurationMode();}
	}
}