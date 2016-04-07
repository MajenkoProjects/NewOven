#include <DSPI.h>
#include <Timer.h>
#include <Picadillo.h>
#include <Framebuffer332.h>
#include <ComfortAA.h>
#include <Display7Seg.h>
#include <Widgets.h>

#include <Average.h>

struct profile {
    int preheatTemp;
    int preheatTime;
    int reflowTemp;
    int reflowTime;
};

struct profile reflow = {
    150, 120000, // 120 second preheat at 150 degrees
    225, 10000   // 10 second reflow at 225 degrees
};

Picadillo tft;
DSPI1 spi;
AnalogTouch ts(LCD_XL, LCD_XR, LCD_YU, LCD_YD, 320, 480);
uint8_t buf[320*100];
Framebuffer332 fb(320, 100, buf);


Average<float> temperature(100); // 10 Seconds worth of data
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
    COOLING
};

enum state phase = IDLE;
volatile uint32_t phaseStarted = 0;
volatile float temperatureNow = 0;
volatile uint32_t reflowStarted = 0;
volatile uint32_t reflowDuration = 0;

Timer4 mainControl;

const uint8_t FAN = 35;
const uint8_t HEAT_LOW = 36;
const uint8_t HEAT_HIGH = 37;

twButton startButton(ts, tft, 0, 300, 320, 70, "START REFLOW");
twButton stopButton(ts, tft, 0, 300, 320, 70, "STOP REFLOW");

twButton fanState(ts, tft, 0, 200, 20, 20, "");
twButton topState(ts, tft, 0, 220, 20, 20, "");
twButton botState(ts, tft, 0, 240, 20, 20, "");

struct thermocouple {
    union {
        struct {
            uint8_t b1;
            uint8_t b2;
            uint8_t b3;
            uint8_t b4;
        } __attribute__((packed));
        struct {
            unsigned    oc:1;
            unsigned    gfault:1;
            unsigned    vfault:1;
            unsigned    res2:1;
            signed      idata:12;
            unsigned    fault:1;
            signed      res1:1;            
            signed      tdata:14;
        } __attribute__((packed));
    } __attribute__((packed));
};

void doStart(Event *e) {
    phase = WARMING;
    stopButton.redraw();
    reflowStarted = millis();
    tlNow.clear();
    tlCav.clear();
}

void doStop(Event *e) {
    phase = IDLE;
    startButton.redraw();
}

void fanOn() {
    digitalWrite(FAN, LOW);
    fanState.setValue(1);
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

void setup() {
    tft.initializeDevice();
    tft.fillScreen(Color::Black);
    ts.initializeDevice();
    ts.scaleX(4.3);
    ts.scaleY(3.3);
    ts.offsetY(5);
    fb.initializeDevice();
    spi.begin();
    pinMode(34, OUTPUT);
    digitalWrite(34, HIGH);
    pinMode(FAN, OPEN);
    pinMode(HEAT_LOW, OPEN);
    pinMode(HEAT_HIGH, OPEN);
    fanOff();
    topOff();
    botOff();

    startButton.onTap(doStart);
    startButton.setBackgroundColor(Color::Green, Color::Green);
    startButton.setFont(Fonts::ComfortAA24);

    stopButton.onTap(doStop);
    stopButton.setBackgroundColor(Color::Red, Color::Red);
    stopButton.setFont(Fonts::ComfortAA24);

    mainControl.attachInterrupt(timeTicker);
    mainControl.setFrequency(10);
    mainControl.start();

    fanState.setBackgroundColor(Color::Gray20, Color::Green);
    topState.setBackgroundColor(Color::Gray20, Color::Green);
    botState.setBackgroundColor(Color::Gray20, Color::Green);

    fanState.setBevel(0);
    topState.setBevel(0);
    botState.setBevel(0);
        
}

void loop() {
    ts.sample();
    

    tft.setCursor(0, 0);
    tft.setTextColor(Color::Goldenrod, Color::Black);
    tft.setFont(Fonts::ComfortAA24);
    tft.println("Oven Temperature:");

    tft.setCursor(150, 30);
    tft.setFont(Fonts::Display7Seg48);
    tft.setTextColor(Color::Red, Color::Black);
    tft.printf("%7.2f", temperatureNow);

    tft.setCursor(0, 90);
    tft.setTextColor(Color::Goldenrod, Color::Black);
    tft.setFont(Fonts::ComfortAA24);
    tft.println("Cavity Temperature:");

    tft.setCursor(150, 120);
    tft.setFont(Fonts::Display7Seg48);
    if (internalTemperature > 80) {
        tft.setTextColor(Color::Red, Color::Black);
    } else if (internalTemperature > 65) {
        tft.setTextColor(Color::Yellow, Color::Black);
    } else {
        tft.setTextColor(Color::Green, Color::Black);
    }
    
    tft.printf("%7.2f", internalTemperature);
    if (phase == IDLE) {
        startButton.render();
    } else {
        stopButton.render();
    }

    fanState.render();
    topState.render();
    botState.render();

    tft.setTextColor(Color::Goldenrod, Color::Black);
    tft.setFont(Fonts::ComfortAA24);
    tft.setCursor(0, 270);

    fb.fillScreen(Color::Black);


    char temp[50];
    switch (phase) {
        case IDLE: sprintf(temp, "Idle"); break;
        case WARMING: sprintf(temp, "Warming: %4d", reflow.preheatTemp); break;
        case PREHEAT: sprintf(temp, "Preheat: %4d", ((reflow.preheatTime - (millis() - phaseStarted)) / 1000)); break;
        case RAMPING: sprintf(temp, "Heating: %4d", reflow.reflowTemp); break;
        case REFLOW: sprintf(temp, "Reflow: %4d", ((reflow.reflowTime - (millis() - phaseStarted)) / 1000)); break;
        case COOLING: sprintf(temp, "Cooling:   50"); break;
    }


    fb.setTextColor(Color::Goldenrod, Color::Goldenrod);
    fb.setFont(Fonts::ComfortAA24);

    int w = fb.stringWidth(temp);
    int h = fb.stringHeight(temp);

    
    for (int i = 0; i < 320; i+=2) {
        fb.setPixel(i, 0, Color::Gray60);
        fb.setPixel(i, 33, Color::Gray60);
        fb.setPixel(i, 66, Color::Gray60);
        fb.setPixel(i, 99, Color::Gray60);
    }

    for (int i = 0; i < 320; i+=12) {
        fb.drawLine(i, 0, i, 100, Color::Gray60);
    }

    for (int i = 0; i < 319; i++) {
        float a = tlCav.get(i) / 3.0;
        float b = tlCav.get(i+1) / 3.0;
        if ((a <= 0.01) || (b <= 0.01)) {
            continue;
        }
        fb.drawLine(i, 100 - a, i+1, 100 - b, Color::Green);
        a = tlNow.get(i) / 3.0;
        b = tlNow.get(i+1) / 3.0;
        if ((a <= 0.01) || (b <= 0.01)) {
            continue;
        }
        fb.drawLine(i, 100 - a, i+1, 100 - b, Color::White);
    }
    fb.setCursor(160 - w/2, 99 - h);
    fb.print(temp);
    fb.draw(tft, 0, 379);
}

void __USER_ISR timeTicker() {
    static int tlTick = 0;
    struct thermocouple tData;
    digitalWrite(34, LOW);
    tData.b4 = spi.transfer((uint8_t)0x00);
    tData.b3 = spi.transfer((uint8_t)0x00);
    tData.b2 = spi.transfer((uint8_t)0x00);
    tData.b1 = spi.transfer((uint8_t)0x00);
    digitalWrite(34, HIGH);
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

    switch(phase) {
        case IDLE: // Make sure NOTHING is running
            fanOff();
            topOff();
            botOff();
            break;
        case WARMING:
            fanOn();
            
            if (predicted < reflow.preheatTemp) {
                botOn();
            } else {
                botOff();
            }
            if (predicted < (reflow.preheatTemp - 1)) {
                topOn();
            } else {
                topOff();
            }

            if (predicted >= reflow.preheatTemp) {
                phaseStarted = millis();
                phase = PREHEAT;
            }

            break;

        case PREHEAT:
            fanOn();
            if (predicted < reflow.preheatTemp) {
                botOn();
            } else {
                botOff();
            }
            if (predicted < (reflow.preheatTemp - 1)) {
                topOn();
            } else {
                topOff();
            }
            if (millis() - phaseStarted >= reflow.preheatTime) {
                phase = RAMPING;
                phaseStarted = millis();
            }
            break;

        case RAMPING:
            fanOn();
            if (predicted < reflow.reflowTemp) {
                botOn();
            } else {
                botOff();
            }
            if (predicted < (reflow.reflowTemp - 1)) {
                topOn();
            } else {
                topOff();
            }
            if (predicted >= reflow.reflowTemp) {
                phaseStarted = millis();
                phase = REFLOW;
            }

            break;
        case REFLOW:
            fanOn();
            if (predicted < reflow.reflowTemp) {
                botOn();
            } else {
                botOff();
            }
            if (predicted < (reflow.reflowTemp - 1)) {
                topOn();
            } else {
                topOff();
            }
            if (millis() - phaseStarted >= reflow.reflowTime) {
                phase = COOLING;
                phaseStarted = millis();
            }
            break;
        case COOLING:
            fanOn();
            topOff();
            botOff();
            if (predicted <= 50) {
                doStop(NULL);
                fanOff();
                phase = IDLE;            
            }
            break;
    }

    
    clearIntFlag(_TIMER_4_IRQ);
}
