#include <Arduino.h>

extern "C" void __cxa_pure_virtual(void) {
    while(1);
}

#define ANALOG_OUTPUT_PIN A2
#define ANALOG_INPUT_PIN A0
#define PUMP_PWM_PIN 44
#define PUMP_ENABLE_PIN 53

/* #define DEBUG */


/* 3 hours */
#define MAX_CYCLE_TIME 180UL * 60UL * 1000UL

/*
 * Pins used for the 7-segment LED - DP,A,B,C,D,E,F,G
 * pattern for segment_patterns also
 */
int led_segment_pins[] = {43, 47,45,41,39,37,49,51};

int segment_patterns [][2] = {
  { 0 ,0x00},
  {'0',0x7E},
  {'1',0x30},
  {'2',0x6d},
  {'3',0x79},
  {'4',0x33},
  {'5',0x5b},
  {'6',0x5f},
  {'7',0x70},
  {'8',0x7f},
  {'9',0x7b}
};

/*
 * The PWM value for the pump - governs how fast it pumps
 */
int pump_pwm_value = 255;
/*
 * How long to keep the pump on in milliseconds
 * This value is contained within the cycle time, not in addition to
 */
unsigned long int pump_on_millis = 3.85 /*minutes*/ * 60UL/* seconds */ * 1000UL /* milliseconds */;
/* testing value */
/* unsigned long int pump_on_millis = 15UL * 1000UL; */

/*
 * Total cycle time in milliseconds from pump start to pump start
 */
unsigned long int total_cycle_millis = 30UL * 60UL * 1000UL;
/* testing value */
/* unsigned long int total_cycle_millis = 100UL * 1000UL; */

unsigned long int next_cycle_time = 0;
unsigned long int max_cycle_time = 0;
unsigned long int pump_state_change = 0;
unsigned long int cycle_start_time = 0;
int current_pump_pwm_value = 0;

void (*animation)(bool start) = NULL;

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  Serial.println("Startup...");
#endif
  analogReference(INTERNAL2V56);

  for(unsigned int i=0;i<8;i++) {
    pinMode(led_segment_pins[i],OUTPUT);
    digitalWrite(led_segment_pins[i],LOW);
  }

  pinMode(ANALOG_OUTPUT_PIN,OUTPUT);
  pinMode(ANALOG_INPUT_PIN,INPUT);
  digitalWrite(ANALOG_INPUT_PIN,HIGH);
  pinMode(PUMP_PWM_PIN,OUTPUT);
  pinMode(PUMP_ENABLE_PIN,OUTPUT);
  analogWrite(PUMP_PWM_PIN,0);
  digitalWrite(PUMP_ENABLE_PIN,HIGH);

  next_cycle_time = pump_on_millis;
  next_cycle_time = 26000;
}

/*
 * Write a bitmask to the onboard 7-segment led
 * Bits are DP,A,B,C,D,E,F,G
 */

void write_bitmask(unsigned char bitmask) {
  for(int i=0;i<8;i++) {
    if(bitmask & 0x80) {
      digitalWrite(led_segment_pins[i],HIGH);
    } else {
      digitalWrite(led_segment_pins[i],LOW);
    }
    bitmask = bitmask << 1;
  }
}

/*
 * Lookup a character in segment_patterns above and output to the LED
 */

void write_char(char ch,bool decimal = false) {
  int i = 0;
  unsigned char bitmask=0;
  for(;i<10;i++) {
    if(segment_patterns[i][0] == ch) {
      break;
    }
  }
  if(i == 10) {return;}
  bitmask = segment_patterns[i][1];
  if(decimal) {
    bitmask |= 0x80;
  }
  write_bitmask(bitmask);
}


/*
 * This is the animation function that gets called when pumping
 * is happening
 */
void pumping_animation(bool start) {
  const unsigned char bitmasks[] = {0x40, 0x20, 0x01, 0x04, 0x08, 0x10, 0x01, 0x02};
  static int current_mask = 0;
  static unsigned long int next_update_time = 0;
  static unsigned long int update_millis = 500;
  if(start) {
    next_update_time = millis() + update_millis;
    current_mask = 0;
    return;
  }
  if(next_update_time > millis()) {
    return;
  }
  next_update_time = millis() + update_millis;

  write_bitmask(bitmasks[current_mask++]);
  if(current_mask > 7) {
    current_mask = 0;
  }
}

/*
 * This is the animation that happens when we're not pumping and not
 * not within 60 seconds of starting a pumping session
 * Displays a value from the photo sensor.  If the photo sensor
 * detects a value normalized to 0 - 9 above 0, it adds that many
 * minutes to the next cycle time.
 */
void sensor_animation(bool start) {
  static unsigned long int next_update_time = 0;
  static unsigned long int update_millis = 500;
  static int max_sensor_value = 0;
  static int min_sensor_value = 2048;
  static bool show_decimal = true;
  int photo_sensor = 0;
  if(start) {
    next_update_time = millis() + update_millis;
    return;
  }
  if(next_update_time > millis()) {
    return;
  }
  next_update_time = millis() + update_millis;

  photo_sensor = analogRead(ANALOG_INPUT_PIN);
  if(photo_sensor > max_sensor_value) {
    max_sensor_value = photo_sensor;
  }
  if(photo_sensor < min_sensor_value && photo_sensor > 0) {
    min_sensor_value = photo_sensor;
  }
  photo_sensor = photo_sensor / 100;
  if(photo_sensor > 9)
    photo_sensor = 9;

  /*
   * Add a minute to the cycle if it's dark and not beyond the max time
   */
  if(photo_sensor && (next_cycle_time < max_cycle_time)) {
    next_cycle_time += (photo_sensor * 60UL * 1000UL);
  }

  write_char(photo_sensor + 0x30,show_decimal);
  show_decimal = !show_decimal;

}

/*
 * This animation is used if we are within 1 minute of the next pump time
 */

void countdown_animation(bool start) {
  static unsigned long int next_update_time = 0;
  static unsigned long int update_millis = 250;
  unsigned long int now = millis();
  int seconds_till_cycle = (next_cycle_time - now) / 1000;
  static bool blink=false;
  if(seconds_till_cycle > 50) {
    update_millis = 1500;
  } else if(seconds_till_cycle > 40) {
    update_millis = 1200;
  } else if(seconds_till_cycle > 30) {
    update_millis = 1000;
  } else if(seconds_till_cycle > 20) {
    update_millis = 800;
  }else if(seconds_till_cycle >= 11) {
    update_millis = 500;
  } else {
    update_millis = 250;
  }

  if(seconds_till_cycle > 9) {
    seconds_till_cycle /= 10;
  }

  if(start) {
    next_update_time = now + update_millis;
    return;
  }
  if(next_update_time > now) {
    return;
  }
  next_update_time = now + update_millis;
  if(blink) {
    write_char(0);
  }
  else {
    write_char(seconds_till_cycle + 0x30);
  }
  blink = !blink;

}

/*
 * Processing loop to check the cycle state and make it do the right thing
 */


void check_cycle_state(unsigned long int now) {
  int seconds_till_cycle = (next_cycle_time - now) / 1000;
  static unsigned long int next_update_time = 0;
  static unsigned long int update_millis = 500;
  if(next_update_time > now) {
    return;
  }
  next_update_time = now + update_millis;
#ifdef DEBUG
  Serial.println("check_cycle_state");
  Serial.print("current_pump_pwm_value = ");
  Serial.println(current_pump_pwm_value);
#endif
  if(seconds_till_cycle > 0 && current_pump_pwm_value == 0) {
    if((seconds_till_cycle < 60) && animation == &sensor_animation) {
      countdown_animation(true);
      animation = &countdown_animation;
    }
  } else if (next_cycle_time <= now) {
    /* time to change */
#ifdef DEBUG
    Serial.print("Now = ");
    Serial.print(now);
    Serial.print(" -- cycle_start_time = ");
    Serial.print(cycle_start_time);
    Serial.print("  --  Next pump change = ");
    Serial.println(pump_state_change + pump_on_millis);
#endif
    if(cycle_start_time && (pump_state_change + pump_on_millis) <= now) {
#ifdef DEBUG
      Serial.println(" ------- Stopping the Cycle  ");
#endif
      next_cycle_time = cycle_start_time + total_cycle_millis;
      max_cycle_time = cycle_start_time + MAX_CYCLE_TIME;
      cycle_start_time = 0;
      current_pump_pwm_value = 0;
      animation = &sensor_animation;
      sensor_animation(true);
      pump_state_change = now;
      analogWrite(PUMP_PWM_PIN,current_pump_pwm_value);
    } else if (current_pump_pwm_value == 0) {
#ifdef DEBUG
      Serial.println(" ------- Starting the Cycle  ");
#endif
      cycle_start_time = now;
      animation = &pumping_animation;
      pumping_animation(true);
      current_pump_pwm_value = pump_pwm_value;
      pump_state_change = now;
      analogWrite(PUMP_PWM_PIN,current_pump_pwm_value);
    }
  }

}


void loop() {
  unsigned long int now = millis();
  if(animation) {
    animation(false);
  }
  if(now > 0) {
    check_cycle_state(now);
  }

}

int main(void)
{
  init();
  setup();
  animation = &sensor_animation;
  for (;;){
    loop();
  }
  return 0;
}
