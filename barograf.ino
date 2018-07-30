/*********************************************************************
 * 
 * Für display-Steuerung siehe Beispiel pcdtest.ino aus 
 * Adafruit_PCD8544 Bibliothek
 * 
 * https://github.com/adafruit/Adafruit-PCD8544-Nokia-5110-LCD-library
 * 
*********************************************************************/
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

/*
 * 
 * http://embedded-lab.com/blog/wp-content/uploads/2015/01/bmp180.zip
 * 
 * BMP180 - Sensor Barometer 
 * 
 * VCC  -> 3.3 V
 * GNG -> GND
 * SDA  -> A4
 * SCL  -> A5
 * 
 */
#include <Wire.h>
#include <BMP180.h>

/*
 * Fonts aus PCD8544 library
 * 
 */
// Font structures for newer Adafruit_GFX (1.1 and later).
// Example fonts are included in 'Fonts' directory.
// To use a font in your Arduino sketch, #include the corresponding .h
// file and pass address of GFXfont struct to setFont().  Pass NULL to
// revert to 'classic' fixed-space bitmap font.
// s. gfxfont.h
#include <Fonts/Picopixel.h>

/*
 * Lightweight low power library for Arduino:
 * 
 * https://github.com/rocketscream/Low-Power
 * 
 */
#include "LowPower.h"

BMP180 barometer;

// Displayanschluss:
// Software SPI (slower updates, more flexible pin options):
// pin 7 - Serial clock out (SCLK)
// pin 6 - Serial data out (DIN)
// pin 5 - Data/Command select (D/C)
// pin 4 - LCD chip select (CS)
// pin 3 - LCD reset (RST)
Adafruit_PCD8544 display = Adafruit_PCD8544(7, 6, 5, 4, 3);

const int wakeUpTaster = 2;          // Der Taster wurde an Pin 2 angeschlossen um Interrup 0 auslösen zu können

long currPress; // 
float currentTemp;

long pressMin[60]; 														// Durchschnitt Minutentakt
long pressStd[60]; 														// Durchschnitt Stundentakt, max 60 Stunden

long pressMaximumFix;    // Maximalegrenze für fixirte Anzeige (3TF)

long pressMaximum, pressMinimum, pressDiff; 	// Maxiamle-, MinimaleDruck und die Differenz
long globalMin, globalMax;										// Maximale und Minimale DruckMessung während der Arbeitszeit des Gerätes
long pressMinAverage, pressStdAverage;				// Durchschnitt pro Minute und pro Stunde

unsigned long lastMillis = millis();
long countSleep = 0;
int sleepZeit = 0;
boolean keyPressed = false;
int countMin, countStd;												// Zaehler: Minuten, Stunden
long countGesamtStd;                          // Stunden Gesamtarbeitszeit

/*
 * 0 - Hauptanzeige
 * 1 - eine Stunde, minutentakt
 * 2 - ein Tag, stundentakt
 * 3 - 2,5 Tage, stundentakt
 * 4 - 2,5 Tage, stundentakt, Fixirte Grenze
 * 5 - Statusanzeige: Globale Min, Max; Akku; Uptime 
 */
byte scr_num = 5;															//  Displayanzeige
boolean refresh = true;												// Aktualisieren Displayanzeige

// Batterie-Pin über Wiederstandbrücke
int pinBat = A1;

void setup()   {
	// für readRaw
	analogReference(INTERNAL);

 // Serial.begin(9600);													// Serial-Port initialisieren
	Wire.begin();																// 
	barometer = BMP180();												// Barometer definieren

	pinMode(wakeUpTaster, INPUT);       // TasterPin als Eingang
	digitalWrite(wakeUpTaster, HIGH);		// interner Pull up Widerstand auf 5V
	
	display.begin();	// init display

	// you can change the contrast around to adapt the display
	// for the best viewing!
	display.setContrast(60);
	display.setTextSize(1);											// normale Textgrösse

	display.display(); 													// show splashscreen
	delay(1000);
	display.clearDisplay();											// clears the screen and buffer

	// Barometer initialisieren
	if(barometer.EnsureConnected()) {
		display.clearDisplay();
		display.setCursor(0,20);  
		display.print("Connected to     BMP180."); // Output we are connected to the computer.
		display.display();
		// When we have connected, we reset the device to ensure a clean start.
		barometer.SoftReset();
		// Now we initialize the sensor and pull the calibration data.
		barometer.Initialize();
		delay(1000);
	} else { 
		// Kein Sensor gefunden
		display.clearDisplay();
		display.setCursor(0,20);  
		display.print( "No BMP sensor" );
		display.display();
		while(1); // Und hier stehen bleiben
	}

	currPress = barometer.GetPressure();				// erste Initialisierungsmessung
	pressMinAverage = currPress;
	globalMax = currPress;
	globalMin = currPress;
	pressMaximumFix = currPress + 500; 					// Hälfte von Anzeigebereich
	attachInterrupt(0, wakeUp, LOW);						// Unterbrechung über Taster aktivieren
}


void loop() {
	if(barometer.IsConnected) {
		// Retrive the current pressure in Pascals.
		long tempPress = barometer.GetPressure() ; // hPa
		currentTemp = barometer.GetTemperature();

		// Maximale Luftdruckunterschied von der letzter Messung = 1000 Pa ( 10 hPa)
		if ( tempPress < currPress + 1000 && tempPress > currPress - 1000 ) {
			currPress = tempPress ; // hPa
//			currPress = tempPress + random( -500, 500); // DEBUG
			if ( globalMax < currPress ) globalMax = currPress;
			if ( globalMin > currPress ) globalMin = currPress;
		}

		putArrays(); // Daten sammeln

		// Anzeige aktualisieren nur wenn nötig
		if ( refresh ) {
			refresh = false;
			// Displayanzeige aktualisieren
			display.clearDisplay();
			switch (scr_num) {
				case 0:		// Hauptbildschirm
					zeigeHauptanzeige();
					refresh = true; // immer aktualisieren
					break;
				case 1: 	// 1 Stunde - minutentakt
					zeigeStunde();
					break;
				case 2:   // 1 Tag - Stundentakt
					zeige1Tag();
					break;
				case 3:		// 2,5 Tagesanzeige - 60 Std
					zeige3Tage();
					break;
				case 4:		// 2,5 Tagesanzeige - 60 Std
					zeige3TageFixed();
					break;
				case 5: // напряжение аккумулятора для контроля
					zeigeStatusanzeige();
					refresh = true;	// immer aktualisieren
					break;
				default:
					break;
			}
			display.display();
		}
	}
	
	// Enter power down state for 1 s with ADC and BOD module disabled
	/*
	 * 	SLEEP_15MS	LITERAL1
	 * 	SLEEP_30MS	LITERAL1
	 * 	SLEEP_60MS	LITERAL1
	 * 	SLEEP_120MS	LITERAL1
	 * 	SLEEP_250MS	LITERAL1
	 * 	SLEEP_500MS	LITERAL1
	 * 	SLEEP_1S	LITERAL1
	 * 	SLEEP_2S	LITERAL1
	 * 	SLEEP_4S	LITERAL1
	 * 	SLEEP_8S	LITERAL1
	 * 	SLEEP_FOREVER	LITERAL1
	*/
	// Einschlafen für 8 Sekunden
	LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);  
	//LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_OFF);  
	if ( !keyPressed ) {
		countSleep = countSleep + 8000;
	} else {
		keyPressed = false;
	}
}

void wakeUp() {
	detachInterrupt(0); // Unterbrechung deaktivieren
	scr_num++;
	if ( scr_num > 5 ) scr_num = 0;
	refresh = true;
	keyPressed = true;
	LowPower.powerDown(SLEEP_250MS, ADC_OFF, BOD_OFF); // Taster entprellen
	countSleep = countSleep + 250;
	attachInterrupt(0, wakeUp, LOW); // Unterbrechung wiederaktivieren
}

void putArrays() {
	// Arrays füllen
	unsigned long time = millis();
	
//	Serial.println( countSleep) ;
	
	if ( ( time - lastMillis + countSleep ) >= 60000 ) {	// 60 Sekunden - eine Minute ist vorbei
		countSleep = countSleep - ( 60000 - ( time - lastMillis ) );
		lastMillis = time; 
		refresh = true;	// jede Minute Anzeige aktualisieren
		countMin = countMin + 1;
		if ( countMin >= 60 ) { 	// eine Stunde ist vorbei
			countMin = countMin - 60;
			countStd = countStd + 1;
			countGesamtStd = countGesamtStd + 1;
			if ( countStd >= 60 ) { 	// Stundenzaehler ist voll
				countStd = countStd - 60;
			} 
			// Stunden
			for ( int i = 0; i < 59; i++) {	// Array verschieben
				pressStd[i] = pressStd[i+1];
			}
			pressStd[59] = pressStdAverage;	// Messung für neue Stunde
			pressStdAverage = pressMinAverage;						// Stundendurchschnitt neu setzen
		}
		// Minuten
		for ( int i = 0; i < 59; i++) {		// Minutenarray verschieben
			pressMin[i] = pressMin[i+1];
		}
		pressMin[59] = pressMinAverage;		// Messung für neue Minute
		pressStdAverage = ( pressStdAverage + pressMinAverage ) / 2;	// Stundendurchschnitt errechnen
		pressMinAverage = currPress;			// Minutendurchschnitt neu setzen
	}

	pressMinAverage = ( pressMinAverage + currPress ) / 2;	// Minutendurchschnitt errechnen
}

void zeigeHauptanzeige() {
	display.setFont(&Picopixel);	// kleiner Text
	display.setCursor(0, 5);  
	display.print("Min: ");
	display.print( (float) globalMin / 100 );
	display.print(", Max: ");
	display.print( (float) globalMax / 100 );
	
	display.setFont();													// Font zurücksetzen
	display.setTextSize(2);											// Doppelte Textgrösse
	display.setCursor( 10, 10);
	display.print( currPress / 100 );						//  aktuelle Luftdruck, ganze Teil
	display.setTextSize(1);											// einfache Textgrösse
	display.setCursor( 47, 17);
	display.print( currPress % 100 );						// aktuelle Luftdruck, Nachkommastellen
	display.print(" hPa");
	display.setTextSize(2);											// Doppelte Textgrösse
	display.setCursor( 10, 30);
	display.print( (long) currentTemp );				// aktuelle Temperatur
	display.setCursor( 35, 37);
	display.setTextSize(1);											// einfache Textgrösse
	display.print( (long)( currentTemp * 100 ) % 100  );	// Nachkommastellen
	display.print(" C");
}

void zeigeStunde() {
	int startIndex = 0;
	zeigeKopf();
	display.setCursor(70,5);  
	display.print("STD");
	if ( pressMin[59] > 0 ) { // wenn Daten vorhanden
		pressMaximum = pressMin[59] + 20;
		pressMinimum = pressMaximum - 40;
		for ( int i = 0; i < 59; i++) {
			if ( pressMin[i] > 0 ) {
				if ( startIndex == 0 ) startIndex = i;
				if ( pressMaximum < pressMin[i] ) {
					pressMaximum = pressMin[i];
				}
				if ( pressMinimum > pressMin[i] ) {
					pressMinimum = pressMin[i];
				}
			}
		}
		
		pressDiff = pressMaximum - pressMinimum;
		int last_y = 0;
		for ( int i = startIndex; i <= 59; i++) {
			int y = 6 + ( 40 * ( pressMaximum - pressMin[i] ) / pressDiff )  ;
			if ( i > startIndex ) { 
				if ( y > 0 && last_y > 0) {
					display.drawLine(i - 1, last_y, i, y, BLACK);
				}
			} 
			last_y =  y;
		}
	} else {
		display.setCursor(10, 20);
		display.print("keine Daten");
	}
	zeigeMinMax(pressMin[59]);
}

void zeige1Tag() {
	int startIndex = 0;
	zeigeKopf();
	display.setCursor(70,5);  
	display.print("1 T");
	if ( pressStd[59] > 0 ) { // wenn Daten vorhanden
		pressMaximum = pressStd[59] + 20;
		pressMinimum = pressMaximum - 40;
		for ( int i = 0; i < 59; i++) {
			if ( pressStd[i] > 0 ) {
				if ( startIndex == 0 && i >= 35) startIndex = i-35;
				if ( pressMaximum < pressStd[i] ) {
					pressMaximum = pressStd[i];
				}
				if ( pressMinimum > pressStd[i] ) {
					pressMinimum = pressStd[i];
				}
			}
		}
		pressDiff = pressMaximum - pressMinimum;
		int last_y;

		for ( int i = startIndex; i <= 24; i++) {
			int y = 6 + ( 40 * ( pressMaximum - pressStd[35+i] ) / pressDiff )  ;
			if ( i > startIndex ) { 
				if ( y > 0 && last_y > 0) {
					display.drawLine( (i - 1) * 2.45, last_y, i * 2.45, y, BLACK);
				}
			}
			last_y =  y;
		}
	} else {
		display.setCursor(10, 20);
		display.print("keine Daten");
	} 
	zeigeMinMax(pressStd[59]);
}

void zeige3Tage() {
	int startIndex = 0;
	zeigeKopf();
	display.setCursor(70,5);  
	display.print("3 T");
	if ( pressStd[59] > 0 ) { // wenn Daten vorhanden
		pressMaximum = pressStd[59] + 20;
		pressMinimum = pressMaximum - 40;
		for ( int i = 0; i < 59; i++) {
			if ( pressStd[i] > 0 ) {
				if ( startIndex == 0 ) startIndex = i;
				if ( pressMaximum < pressStd[i] ) {
					pressMaximum = pressStd[i];
				}
				if ( pressMinimum > pressStd[i] ) {
					pressMinimum = pressStd[i];
				}
			}
		}
		pressDiff = pressMaximum - pressMinimum;
		int last_y;
		for ( int i = startIndex; i <= 59; i++) {
			int y = 6 + ( 40 * ( pressMaximum - pressStd[i] ) / pressDiff )  ;
			if ( i > startIndex ) { 
				if ( y > 0 && last_y > 0) {
					display.drawLine(i - 1, last_y, i, y, BLACK);
				}
			}
			last_y =  y;
		}
		
		// Tagesraster
    for ( int i =  59-24; i > 0; i = i - 24) {
      for ( int j = 6; j < 47; j = j + 5) {
        display.drawLine( i, j, i, j + 2, BLACK);    
      }
    }
    
	} else {
		display.setCursor(10, 20);
		display.print("keine Daten");
	} 


	zeigeMinMax(pressStd[59]);
}

void zeige3TageFixed() {
	int startIndex = 0;
	zeigeKopf();
	display.setCursor(70,5);  
	display.print("3TF");
	if ( pressStd[59] > 0 ) { // wenn Daten vorhanden
		pressDiff = 1000;  // Anzeigebereich 10 hPa
		for (int i = 0; i < 59 ; i++) {   // Anzeigefenster finden
			if ( pressStd[i] > 0 ) {
				if ( startIndex == 0 ) startIndex = i;
				if (pressMaximumFix < pressStd[i]) {
					pressMaximumFix = pressMaximumFix + pressDiff/2;         // Graph auf die Hälfte verschieben
				} 
				if ((pressMaximumFix - pressDiff) > pressStd[i]) {
					pressMaximumFix = pressMaximumFix - pressDiff/2;
				} 
			}
		}
		pressMaximum = pressMaximumFix;
		pressMinimum = pressMaximum - pressDiff;
		
		int last_y;
		for ( int i = startIndex; i <= 59; i++) {
			int y = map(pressStd[i], pressMaximum, pressMinimum, 6, 48);
			y = constrain(y, 6, 47);
			if ( i > startIndex ) { 
				if ( y > 0 && last_y > 0) {
					display.drawLine(i - 1, last_y, i, y, BLACK);
				}
			}
			last_y =  y;
		}
		
		// Tagesraster
    for ( int i =  59-24; i > 0; i = i - 24) {
      for ( int j = 6; j < 47; j = j + 5) {
        display.drawLine( i, j, i, j + 2, BLACK);    
      }
    }
    
	} else {
		display.setCursor(10, 20);
		display.print("keine Daten");
	} 

	zeigeMinMax(pressStd[59]);
}

void zeigeStatusanzeige() {
  display.setFont(&Picopixel);  // kleiner Text
  display.setCursor(0, 5);  
  display.print("Min: ");
  display.print( (float) globalMin / 100 );
  display.print(", Max: ");
  display.print( (float) globalMax / 100 );
  
  display.setFont();                          // Font zurücksetzen
  display.setTextSize(1);                     // einfache Textgrösse
  display.setCursor( 10, 10);
  display.print("Bat:");    
  display.print( readRaw() );    // Akku-Spannung
  display.setTextSize(1);                     // einfache Textgrösse
  display.setCursor( 10, 20);
  display.print("Up: ");
  display.setCursor( 30, 20);
  display.print( countGesamtStd / 24 );				// komplette Tage
  display.print("t ");
  display.setCursor( 30, 30);
  display.print( countGesamtStd % 24 );				// komplette Stunden
  display.print("std");
  display.setCursor( 30, 40);
  display.print( countMin );									// Minuten
  display.print("min");
}

void zeigeKopf() {
	display.setFont(&Picopixel);	// kleiner Text
	display.setCursor(0,5);  
	display.print( (float) currPress / 100);
	display.print( " hPa, ");
	display.print( currentTemp );
	display.print( " C");
}

void zeigeMinMax(long pressure) {
	display.setCursor(60,13);
	display.print( (float) pressMaximum / 100 );  
	display.setCursor(60,30);
	display.print( (float) pressure / 100 );  
	display.setCursor(60,47);
	display.print( (float) pressMinimum / 100 );
}

/*
 * https://vk-book.ru/uroven-zaryada-akkumulyatora-18650-na-arduino/
 * 
 *   RAW ------+
 *             |
 *            10kOhm
 *             |
 *             +-------> A0 (pinBat)
 *             |
 *            1kOhm
 *             |
 *   GND ------+
 */
float readRaw() {
	float Vbat = (analogRead(pinBat) * 1.1) / 1023;
	//float del = 0.0925; // R2/(R1+R2)  0.99кОм / (9.88кОм + 0.99кОм)
	float del = 0.0930; // R2/(R1+R2)  0.99кОм / (9.88кОм + 0.99кОм)
	float Vin = Vbat / del;
	return Vin;
}
