#include <DSPI.h>
#include <Picadillo.h>
#include <Framebuffer332.h>
#include <ComfortAA.h>
#include <Display7SegShadow.h>
#include <Widgets.h>
#include <Average.h>
#include <EEPROM.h>
#include <MonoIcon.h>
#include <Brankic.h>
#include <XTerm.h>

// Rows - driven low
const uint8_t KP_R0 = 26;
const uint8_t KP_R1 = 27;
const uint8_t KP_R2 = 28;
const uint8_t KP_R3 = 29;
// Columns - pulled high
const uint8_t KP_C0 = 30;
const uint8_t KP_C1 = 31;
const uint8_t KP_C2 = 32;
const uint8_t KP_C3 = 33;
// Pullup resistor high pins for columns
const uint8_t KP_P0 = 4;
const uint8_t KP_P1 = 5;
const uint8_t KP_P2 = 6;
const uint8_t KP_P3 = 7;

const uint8_t THERMO = 34;
const uint8_t FAN = 35;
const uint8_t HEAT_LOW = 36;
const uint8_t HEAT_HIGH = 37;

const uint8_t AUDENB = 8;
const uint8_t AUDIO = 9;

Picadillo tft;
DSPI1 spi;
AnalogTouch ts(LCD_XL, LCD_XR, LCD_YU, LCD_YD, 320, 480);
uint8_t buf[320 * 130];
Framebuffer332 fb(320, 130, buf);

const uint8_t rows[4] = {KP_R0, KP_R1, KP_R2, KP_R3};
const uint8_t cols[4] = {KP_C0, KP_C1, KP_C2, KP_C3};
const uint8_t pullup[4] = {KP_P0, KP_P1, KP_P2, KP_P3};

Average<float> temperature(10); // 1 Seconds worth of data
Average<float> tlNow(320);
Average<float> tlCav(320);

float internalTemperature;
int targetTemperature;

enum state {
	IDLE,
	WARMING,
	PREHEAT,
	RAMPING,
	REFLOW,
	COOLING,
    FASTCOOL,
	BAKE
};

enum state phase = IDLE;
volatile uint32_t phaseStarted = 0;
volatile float temperatureNow = 0;
volatile uint32_t reflowStarted = 0;
volatile uint32_t reflowDuration = 0;

MonoIcon startButton(ts, tft, 8, 408, 64, 64, MonoIcon::MonoIconBG, Brankic::ChartIncreasing, Color::Gray60, "Reflow", Fonts::XTerm, Color::White);
MonoIcon bakeButton(ts, tft, 88, 408, 64, 64, MonoIcon::MonoIconBG, Brankic::Oven, Color::Gray60, "Bake", Fonts::XTerm, Color::White);
MonoIcon stopButton(ts, tft, 168, 408, 64, 64, MonoIcon::MonoIconBG, Brankic::ErrorOpen, Color::Gray60, "Stop", Fonts::XTerm, Color::White);
MonoIcon settingsButton(ts, tft, 248, 408, 64, 64, MonoIcon::MonoIconBG, Brankic::Settings2, Color::Gray60, "Settings", Fonts::XTerm, Color::White);

//twButton startButton(ts, tft,  0, 300, 160, 70, "REFLOW");
//twButton stopButton(ts, tft,   0, 300, 320, 70, "STOP");
//twButton bakeButton(ts, tft, 160, 300, 160, 70, "BAKE");

//twButton settingsButton(ts, tft, 0, 230, 160, 70, "SETTINGS");

twButton fanState(ts, tft, 20, 150, 20, 20, "");
twButton topState(ts, tft, 20, 175, 20, 20, "");
twButton botState(ts, tft, 20, 200, 20, 20, "");

bool inSettings = false;

twButton setPreTemp(ts, tft, 0, 30, 160, 50, "Preheat Temp");
twButton setPreTime(ts, tft, 0, 80, 160, 50, "Preheat Time");
twButton setRefTemp(ts, tft, 0, 130, 160, 50, "Reflow Temp");
twButton setRefTime(ts, tft, 0, 180, 160, 50, "Reflow Time");
twButton setBakeTemp(ts, tft, 0, 230, 160, 50, "Bake Temp");
twButton setMeltTemp(ts, tft, 0, 280, 160, 50, "Melt Temp");
twButton setFan(ts, tft, 10, 345, 30, 30, "");
twButton setReturn(ts, tft, 0, 410, 320, 70, "Save");
struct settings {
	uint8_t fanEnabled;
	uint16_t bakeTemperature;
	uint16_t preheatTemperature;
	uint16_t preheatTime;
	uint16_t reflowTemperature;
	uint16_t reflowTime;
    uint16_t meltTemperature;
} __attribute__((packed));

struct settings config;

struct thermocouple {
	union {
		struct {
			uint8_t b1;
			uint8_t b2;
			uint8_t b3;
			uint8_t b4;
		} __attribute__((packed));
		struct {
			unsigned    oc: 1;
			unsigned    gfault: 1;
			unsigned    vfault: 1;
			unsigned    res2: 1;
			signed      idata: 12;
			unsigned    fault: 1;
			signed      res1: 1;
			signed      tdata: 14;
		} __attribute__((packed));
	} __attribute__((packed));
};

const char *alertText = NULL;
uint32_t alertCount = 0;

void alert(const char *text) {
    alertCount = 100;
    alertText = text;
}

void pip() {
    tone(AUDIO, 1000, 100);
}

void click() {
    pinMode(AUDIO, OUTPUT);
    delay(1);
    digitalWrite(AUDIO, LOW);
    delay(1);
    digitalWrite(AUDIO, HIGH);
    delay(1);
    digitalWrite(AUDIO, LOW);
}

void doStart(Event *e) {
    click();
	phase = WARMING;
	stopButton.setEnabled(true);
	startButton.setEnabled(false);
	bakeButton.setEnabled(false);
	reflowStarted = millis();
	tlNow.clear();
	tlCav.clear();
}

void doBake(Event *e) {
    click();
	phase = BAKE;
	stopButton.setEnabled(true);
	startButton.setEnabled(false);
	bakeButton.setEnabled(false);
	reflowStarted = millis();
	tlNow.clear();
	tlCav.clear();
}

void doStop(Event *e) {
    click();
	phase = IDLE;
	stopButton.setEnabled(false);
	startButton.setEnabled(true);
	bakeButton.setEnabled(true);
}

void fanOn() {
	if (config.fanEnabled) {
		digitalWrite(FAN, LOW);
		fanState.setValue(1);
	}
}

void fanOff() {
	digitalWrite(FAN, HIGH);
	fanState.setValue(0);
}

void topOn() {
	digitalWrite(HEAT_HIGH, LOW);
	topState.setValue(1);
}

void topOff() {
	digitalWrite(HEAT_HIGH, HIGH);
	topState.setValue(0);
}

void botOn() {
	digitalWrite(HEAT_LOW, LOW);
	botState.setValue(1);
}

void botOff() {
	digitalWrite(HEAT_LOW, HIGH);
	botState.setValue(0);
}

void toggleFan(Event *e) {
	config.fanEnabled = config.fanEnabled ? 0 : 1;
	setFan.setValue(config.fanEnabled);
}

void setBack(Event *e) {
    click();
	inSettings = false;
}

int setValueSelected = 0;

void selectPreTemp(Event *e) {
    click();
	setValueSelected = 1;
	config.preheatTemperature = 0;
}

void selectPreTime(Event *e) {
    click();
	setValueSelected = 2;
	config.preheatTime = 0;
}

void selectRefTemp(Event *e) {
    click();
	setValueSelected = 3;
	config.reflowTemperature = 0;
}

void selectRefTime(Event *e) {
    click();
	setValueSelected = 4;
	config.reflowTime = 0;
}

void selectBakeTemp(Event *e) {
    click();
	setValueSelected = 5;
	config.bakeTemperature = 0;
}

void selectMeltTemp(Event *e) {
    click();
    setValueSelected = 6;
    config.meltTemperature = 0;
}

void doSettings(Event *e) {
    click();
	char temp[50];
	setValueSelected = 0;
	inSettings = true;
	tft.fillScreen(Color::Black);
	setPreTemp.redraw();
	setPreTime.redraw();
	setRefTemp.redraw();
	setRefTime.redraw();
	setBakeTemp.redraw();
    setMeltTemp.redraw();
	setFan.redraw();
	setReturn.redraw();
	setPreTemp.setFont(Fonts::ComfortAA16);
	setPreTime.setFont(Fonts::ComfortAA16);
	setRefTemp.setFont(Fonts::ComfortAA16);
	setRefTime.setFont(Fonts::ComfortAA16);
	setBakeTemp.setFont(Fonts::ComfortAA16);
    setMeltTemp.setFont(Fonts::ComfortAA16);
	setFan.setValue(config.fanEnabled);
	setFan.setBackgroundColor(Color::Red, Color::Green);
	setFan.onTap(toggleFan);
	setReturn.setBackgroundColor(Color::Green, Color::Green);
	setReturn.setFont(Fonts::ComfortAA24);
	setReturn.onTap(setBack);
	setPreTemp.onTap(selectPreTemp);
	setPreTime.onTap(selectPreTime);
	setRefTemp.onTap(selectRefTemp);
	setRefTime.onTap(selectRefTime);
	setBakeTemp.onTap(selectBakeTemp);
    setMeltTemp.onTap(selectMeltTemp);
	printAround("Settings", 160, 0, Color::Goldenrod, Fonts::ComfortAA24);

	while (inSettings) {
		ts.sample();
		sprintf(temp, "%3d", config.preheatTemperature);
		printAround(temp, 240, 30, setValueSelected == 1 ? Color::Red : Color::White, Fonts::Display7SegShadow48);
		sprintf(temp, "%3d", config.preheatTime);
		printAround(temp, 240, 80, setValueSelected == 2 ? Color::Red : Color::White, Fonts::Display7SegShadow48);
		sprintf(temp, "%3d", config.reflowTemperature);
		printAround(temp, 240, 130, setValueSelected == 3 ? Color::Red : Color::White, Fonts::Display7SegShadow48);
		sprintf(temp, "%3d", config.reflowTime);
		printAround(temp, 240, 180, setValueSelected == 4 ? Color::Red : Color::White, Fonts::Display7SegShadow48);
		sprintf(temp, "%3d", config.bakeTemperature);
		printAround(temp, 240, 230, setValueSelected == 5 ? Color::Red : Color::White, Fonts::Display7SegShadow48);
        sprintf(temp, "%3d", config.meltTemperature);
        printAround(temp, 240, 280, setValueSelected == 6 ? Color::Red : Color::White, Fonts::Display7SegShadow48);
		setPreTemp.render();
		setPreTime.render();
		setRefTemp.render();
		setRefTime.render();
		setBakeTemp.render();
        setMeltTemp.render();
		setFan.render();
		tft.setCursor(50, 350);
		tft.setFont(Fonts::ComfortAA24);
		tft.setTextColor(Color::Goldenrod, Color::Black);
		tft.print("Fan Enabled");
		setReturn.render();
		char kpval = scanKb();

		if (kpval != 0) {
    click();
			if (kpval == '#') {
				setValueSelected = 0;
			}

			if (setValueSelected == 1) {
				if (kpval >= '0' && kpval <= '9') {
					config.preheatTemperature *= 10;
					config.preheatTemperature += (kpval - '0');
					config.preheatTemperature %= 1000;
				}

				if (kpval == '*') {
					config.preheatTemperature /= 10;
				}
			}

			if (setValueSelected == 2) {
				if (kpval >= '0' && kpval <= '9') {
					config.preheatTime *= 10;
					config.preheatTime += (kpval - '0');
					config.preheatTime %= 1000;
				}

				if (kpval == '*') {
					config.preheatTime /= 10;
				}
			}

			if (setValueSelected == 3) {
				if (kpval >= '0' && kpval <= '9') {
					config.reflowTemperature *= 10;
					config.reflowTemperature += (kpval - '0');
					config.reflowTemperature %= 1000;
				}

				if (kpval == '*') {
					config.reflowTemperature /= 10;
				}
			}

			if (setValueSelected == 4) {
				if (kpval >= '0' && kpval <= '9') {
					config.reflowTime *= 10;
					config.reflowTime += (kpval - '0');
					config.reflowTime %= 1000;
				}

				if (kpval == '*') {
					config.reflowTime /= 10;
				}
			}

			if (setValueSelected == 5) {
				if (kpval >= '0' && kpval <= '9') {
					config.bakeTemperature *= 10;
					config.bakeTemperature += (kpval - '0');
					config.bakeTemperature %= 1000;
				}

				if (kpval == '*') {
					config.bakeTemperature /= 10;
				}
			}

            if (setValueSelected == 6) {
                if (kpval >= '0' && kpval <= '9') {
                    config.meltTemperature *= 10;
                    config.meltTemperature += (kpval - '0');
                    config.meltTemperature %= 1000;
                }

                if (kpval == '*') {
                    config.meltTemperature /= 10;
                }
            }
}
	}

	tft.fillScreen(Color::Black);
//    if (phase == IDLE) {
	startButton.redraw();
	bakeButton.redraw();
//    } else {
	stopButton.redraw();
//    }
	settingsButton.redraw();
	fanState.redraw();
	topState.redraw();
	botState.redraw();
	saveSettings();
}


bool initKb() {
	for (int r = 0; r < 4; r++) {
		pinMode(rows[r], INPUT);
		pinMode(cols[r], INPUT);
		pinMode(pullup[r], OUTPUT);
		digitalWrite(pullup[r], HIGH);
	}
}

char scanKb() {
	static uint32_t last = 0;
	uint32_t out = 0;

	for (int r = 0; r < 4; r++) {
		pinMode(rows[r], OUTPUT);
		digitalWrite(rows[r], LOW);

		for (int c = 0; c < 4; c++) {
			int v = digitalRead(cols[c]) == LOW;
			v <<= c;
			v <<= (r * 4);
			out |= v;
		}

		pinMode(rows[r], INPUT);
	}

	// Find which have changed state
	uint32_t xr = out ^ last;
	uint32_t pressed = out & xr;
	last = out;

	if (pressed & 0x0001) {
		return '1';
	}

	if (pressed & 0x0010) {
		return '2';
	}

	if (pressed & 0x0100) {
		return '3';
	}

	if (pressed & 0x1000) {
		return 'A';
	}

	if (pressed & 0x0002) {
		return '4';
	}

	if (pressed & 0x0020) {
		return '5';
	}

	if (pressed & 0x0200) {
		return '6';
	}

	if (pressed & 0x2000) {
		return 'B';
	}

	if (pressed & 0x0004) {
		return '7';
	}

	if (pressed & 0x0040) {
		return '8';
	}

	if (pressed & 0x0400) {
		return '9';
	}

	if (pressed & 0x4000) {
		return 'C';
	}

	if (pressed & 0x0008) {
		return '*';
	}

	if (pressed & 0x0080) {
		return '0';
	}

	if (pressed & 0x0800) {
		return '#';
	}

	if (pressed & 0x8000) {
		return 'D';
	}

	return 0;
}

void loadSettings() {
	uint8_t *s = (uint8_t *)&config;

	for (int i = 0; i < sizeof(struct settings); i++) {
		s[i] = EEPROM.read(i);
	}

	config.preheatTemperature %= 1000;
	config.preheatTime %= 1000;
	config.reflowTemperature %= 1000;
	config.reflowTime %= 1000;
	config.bakeTemperature %= 1000;
    config.meltTemperature %= 1000;
}

void saveSettings() {
	uint8_t *s = (uint8_t *)&config;

	for (int i = 0; i < sizeof(struct settings); i++) {
		EEPROM.write(i, s[i]);
	}
}

void iconFlasher(int id, void *tptr) {
	static bool onoff = 0;
	onoff = !onoff;

	switch (phase) {
		case IDLE:
			startButton.setColor(Color::Green);
			bakeButton.setColor(Color::Green);
			stopButton.setColor(Color::Gray60);
			settingsButton.setColor(Color::SkyBlue);
			break;

		case WARMING:
		case PREHEAT:
		case RAMPING:
		case REFLOW:
		case COOLING:
        case FASTCOOL:
			startButton.setColor(onoff ? Color::Red : Color::Green);
			bakeButton.setColor(Color::Gray60);
			stopButton.setColor(Color::Red);
			settingsButton.setColor(Color::SkyBlue);
			break;

		case BAKE:
			bakeButton.setColor(onoff ? Color::Red : Color::Green);
			startButton.setColor(Color::Gray60);
			stopButton.setColor(Color::Red);
			settingsButton.setColor(Color::SkyBlue);
			break;
	}
}

void setup() {
    pinMode(AUDENB, OUTPUT);
    digitalWrite(AUDENB, HIGH);
    
	initKb();
	
	loadSettings();
	tft.initializeDevice();
	tft.fillScreen(Color::Black);
	ts.initializeDevice();
	ts.scaleX(4.3);
	ts.scaleY(3.3);
	ts.offsetY(5);
	fb.initializeDevice();
	spi.begin();
	pinMode(THERMO, OUTPUT);
	digitalWrite(THERMO, HIGH);
	pinMode(FAN, OPEN);
	pinMode(HEAT_LOW, OPEN);
	pinMode(HEAT_HIGH, OPEN);
	fanOff();
	topOff();
	botOff();
	settingsButton.onTap(doSettings);
//    settingsButton.setBackgroundColor(Color::SkyBlue, Color::SkyBlue);
	settingsButton.setFont(Fonts::ComfortAA24);
	startButton.onTap(doStart);
//    startButton.setBackgroundColor(Color::Green, Color::Green);
	startButton.setFont(Fonts::ComfortAA24);
	bakeButton.onTap(doBake);
//    bakeButton.setBackgroundColor(Color::Green, Color::Green);
	bakeButton.setFont(Fonts::ComfortAA24);
	stopButton.onTap(doStop);
//    stopButton.setBackgroundColor(Color::Red, Color::Red);
	stopButton.setFont(Fonts::ComfortAA24);
	fanState.setBackgroundColor(Color::Gray20, Color::Green);
	topState.setBackgroundColor(Color::Gray20, Color::Green);
	botState.setBackgroundColor(Color::Gray20, Color::Green);
	fanState.setBevel(0);
	topState.setBevel(0);
	botState.setBevel(0);
	createTask(timeTicker, 100, TASK_ENABLE, NULL);
	createTask(iconFlasher, 250, TASK_ENABLE, NULL);
}

static inline void printAround(const char *txt, int x, int y, color_t color, const uint8_t *font) {
	tft.setTextColor(color, Color::Black);
	tft.setFont(font);
	int w = tft.stringWidth(txt);
	tft.setCursor(x - w / 2, y);
	tft.print(txt);
}

void loop() {
    static uint32_t alertBlink = millis();
    static bool alertBlinkColor = false;
    
	char temp[50];
	ts.sample();
	printAround("Oven", 80, 20, Color::Goldenrod, Fonts::ComfortAA24);
	printAround("Cavity", 240, 20, Color::Goldenrod, Fonts::ComfortAA24);
	sprintf(temp, "%6.2f", temperatureNow);
	printAround(temp, 80, 60, Color::Red, Fonts::Display7SegShadow48);
	color_t ccol = Color::Green;

	if (internalTemperature > 80) {
		ccol = Color::Red;
	} else if (internalTemperature > 65) {
		ccol = Color::Yellow;
	}

	sprintf(temp, "%6.2f", internalTemperature);
	printAround(temp, 240, 60, ccol, Fonts::Display7SegShadow48);
	stopButton.render();
	startButton.render();
	bakeButton.render();
	settingsButton.render();
	fanState.render();
	topState.render();
	botState.render();
	tft.setFont(Fonts::ComfortAA24);
	tft.setTextColor(Color::Goldenrod, Color::Goldenrod);
	tft.setCursor(50, 150);
	tft.print("Fan");
	tft.setCursor(50, 175);
	tft.print("Top Heater");
	tft.setCursor(50, 200);
	tft.print("Bottom Heater");
	tft.setTextColor(Color::Goldenrod, Color::Black);
	tft.setFont(Fonts::ComfortAA24);
	tft.setCursor(0, 270);
	fb.fillScreen(Color::Black);

	switch (phase) {
		case IDLE:
			sprintf(temp, "Idle");
			break;

		case WARMING:
			sprintf(temp, "Warming: %4d", config.preheatTemperature);
			break;

		case PREHEAT:
			sprintf(temp, "Preheat: %4d", (((config.preheatTime * 1000) - (millis() - phaseStarted)) / 1000));
			break;

		case RAMPING:
			sprintf(temp, "Heating: %4d", config.reflowTemperature);
			break;

		case REFLOW:
			sprintf(temp, "Reflow: %4d", (((config.reflowTime * 1000) - (millis() - phaseStarted)) / 1000));
			break;

		case COOLING:
			sprintf(temp, "Cooling: %4d", config.meltTemperature);
			break;

        case FASTCOOL:
            sprintf(temp, "Cooling:   50");
            break;

		case BAKE:
			sprintf(temp, "Baking:  %4d", config.bakeTemperature);
			break;
	}

	fb.setTextColor(Color::Goldenrod, Color::Goldenrod);
	fb.setFont(Fonts::ComfortAA24);
	int w = fb.stringWidth(temp);
	int h = fb.stringHeight(temp);
	int divs = (config.reflowTemperature / 100) + 1;

	for (int i = 0; i < 320; i += 2) {
		for (int j = 0; j < 100; j += (100 / divs)) {
			fb.setPixel(i, j, Color::Gray60);
		}

		fb.setPixel(i, 99, Color::Gray60);
        fb.setPixel(i, 100 - config.preheatTemperature/divs, Color::Yellow);
        fb.setPixel(i, 100 - config.reflowTemperature/divs, Color::Red);
	}

	for (int i = 0; i < 320; i += 12) {
		fb.drawLine(i, 0, i, 100, Color::Gray60);
	}

	for (int i = 0; i < 319; i++) {
		float a = tlCav.get(i) / (float)divs;
		float b = tlCav.get(i + 1) / (float)divs;

		if ((a <= 0.01) || (b <= 0.01)) {
			continue;
		}

		fb.drawLine(i, 100 - a, i + 1, 100 - b, Color::DarkGreen);
		a = tlNow.get(i) / (float)divs;
		b = tlNow.get(i + 1) / (float)divs;

		if ((a <= 0.01) || (b <= 0.01)) {
			continue;
		}

		fb.drawLine(i, 100 - a, i + 1, 100 - b, Color::White);
	}

	fb.setCursor(160 - w / 2, 129 - h);
	fb.print(temp);

    if (alertCount > 0 && alertText != NULL) {
        if (millis() - alertBlink >= 500) {
            alertBlink = millis();
            alertBlinkColor = !alertBlinkColor;
            fb.setFont(Fonts::ComfortAA24);
            fb.setTextColor(alertBlinkColor ? Color::Red : Color::Yellow,
                            alertBlinkColor ? Color::Red : Color::Yellow);
            w = fb.stringWidth(alertText);
            h = fb.stringHeight(alertText);
            fb.setCursor(160 - w/2, 50 - h/2);
            fb.print(alertText);
            tone(AUDIO, alertBlinkColor ? 1000 : 500, 500);
            alertCount--;
        }
    }
 
	fb.draw(tft, 0, 260);
}

void timeTicker(int id, void *tptr) {
	static int tlTick = 0;
	struct thermocouple tData;
	digitalWrite(THERMO, LOW);
	tData.b4 = spi.transfer((uint8_t)0x00);
	tData.b3 = spi.transfer((uint8_t)0x00);
	tData.b2 = spi.transfer((uint8_t)0x00);
	tData.b1 = spi.transfer((uint8_t)0x00);
	digitalWrite(THERMO, HIGH);
	temperatureNow = tData.tdata * 0.25;
	temperature.push(temperatureNow);
	internalTemperature = tData.idata * 0.0625;
	float predicted = temperatureNow; //temperature.predict(50);
	tlTick++;

	if (tlTick == 50) {
		tlTick = 0;

		if (phase != IDLE) {
			tlNow.push(temperatureNow);
			tlCav.push(internalTemperature);
		}
	}

	switch (phase) {
		case IDLE: // Make sure NOTHING is running
			fanOff();
			topOff();
			botOff();
			break;

		case WARMING:
			fanOn();
			fanOn();
			botOn();
			topOn();

			if (predicted >= config.preheatTemperature) {
				phaseStarted = millis();
				phase = PREHEAT;
                pip();
			}

			break;

		case PREHEAT:
			fanOn();

			if (predicted < config.preheatTemperature) {
				botOn();
			} else {
				botOff();
			}

			if (predicted < (config.preheatTemperature - 1)) {
				topOn();
			} else {
				topOff();
			}

			if (millis() - phaseStarted >= (config.preheatTime * 1000)) {
                pip();
				phase = RAMPING;
				phaseStarted = millis();
			}

			break;

		case RAMPING:
			fanOn();
			botOn();
			topOn();

			if (predicted >= config.reflowTemperature) {
                pip();
				phaseStarted = millis();
				phase = REFLOW;
			}

			break;

		case REFLOW:
			fanOn();

			if (predicted < config.reflowTemperature) {
				botOn();
				topOn();
			} else {
				botOff();
				topOff();
			}

			if (millis() - phaseStarted >= (config.reflowTime * 1000)) {
                pip();
				phase = COOLING;
				phaseStarted = millis();
			}

			break;

        case COOLING:
            fanOn();
            topOff();
            botOff();

            if (predicted <= config.meltTemperature) {
                pip();
                phase = FASTCOOL;
                alert("OPEN THE DOOR");
            }

            break;

		case FASTCOOL:
			fanOn();
			topOff();
			botOff();

			if (predicted <= 50) {
                pip();
				doStop(NULL);
				fanOff();
				phase = IDLE;
			}

			break;

		case BAKE:
			fanOn();

			if (predicted < config.bakeTemperature) {
				botOn();
			} else {
				botOff();
			}

			if (predicted < config.bakeTemperature - 1) {
				topOn();
			} else {
				topOff();
			}

			break;
	}
}
