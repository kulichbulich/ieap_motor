// Neblokujici "heartbeat" LED na IO38 (J4, "volny pin" v board_pins.h).
//
// Zavolej jednou ze setup(). Dal uz blika sama na pozadi (esp_timer
// periodicky prerusuje nezavisle na loop()), 80 ms rozsviceno / 80 ms
// zhasnuto, po celou dobu behu firmwaru - i kdyz zrovna nejaky test ceka
// v delay() na klavesu.
//
// Externi LED + ~330R pripoj mezi J4 a GND (stejne zapojeni jako u
// t01_blink_io38).

#pragma once

void status_led_begin();

// Testy t00..t02 si na IO38 sahaji samy (nebo je to pin, ktery primo
// overuji) - heartbeat by jim pletl vysledky, proto si ho na svem zacatku
// vypnou. status_led_resume() se vola po kazdem testu bez ohledu na to,
// jestli byl pozastaveny - kdyz nebyl, nic nedela.
void status_led_pause();
void status_led_resume();
