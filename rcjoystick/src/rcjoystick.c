#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <linux/joystick.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common/mavlink.h"

#define BUFFER_LENGTH 2041

int errsv;
uint8_t axes_count = 5;

///////////////////////////////////////////////////////////////////////////////////////  
int read_event(int fd, struct js_event *event)
{
    ssize_t bytes;

    bytes = read(fd, event, sizeof(*event));
    if (bytes == sizeof(*event))
        return 0;

    errsv = errno;
    return -1;
}

size_t get_axes_count(int fd)
{
    __u8 axes;

    if (ioctl(fd, JSIOCGAXES, &axes) == -1)
        return 0;

    return axes;
}

size_t get_button_count(int fd)
{
    __u8 buttons;
    if (ioctl(fd, JSIOCGBUTTONS, &buttons) == -1)
        return 0;

    return buttons;
}

struct axis_state {
    short x, y;
};

size_t get_axis_state(struct js_event *event, struct axis_state axes[3])
{
    size_t axis = event->number / 2;

    if (axis < axes_count)
    {
        if (event->number % 2 == 0)
            axes[axis].x = event->value;
        else
            axes[axis].y = event->value;
    }

    return axis;
}

///////////////////////////////////////////////////////////////////////////////////////
long long millis() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
    return milliseconds;
}

uint16_t axes_to_ch(int16_t val) {
    return ((32768 + val) / 65.535) + 1000;
}

///////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
  const char *device;
  int js;
  struct js_event event;
  struct axis_state axes[9] = {
      { 0, 0 },           //ch1/2 default
      { -32768, 0 },      //ch3/4 default
      { -32768, -32768 }, //ch5/6 default
      { -32768, -32768 }, //ch7/8 default
      { -32768, -32768 }, //ch9/10 default
      { -32768, -32768 }, //ch11/12 default
      { -32768, -32768 }, //ch13/14 default
      { -32768, -32768 }, //ch15/16 default
      { -32768, -32768 }, //ch17/18 default
      };
  size_t axis;

  //udp sock
  uint8_t buf[BUFFER_LENGTH];
  mavlink_message_t msg;
  uint16_t len;
  int bytes_sent;
  int out_sock = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in sin_out = {
      .sin_family = AF_INET,
  };

  int buttons[14] = {1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,1000};
  
  //time checker
  long long time_check = millis();
  uint16_t send_time = 50;
  struct timespec tw = {0,10};
  struct timespec tr;
  //args
  int opt;
  bool verbose = false;
  //defaults
  device = "/dev/input/js0";
  inet_aton("127.0.0.1", &sin_out.sin_addr);
  sin_out.sin_port = htons(14650);
  
  while ((opt = getopt(argc, argv, "vd:a:p:t:x:h")) != -1) {
        switch (opt) {
        case 'v':
            verbose = true;
            break;
        case 'd':
            device = optarg;
            break;
        case 'a':
            inet_aton(optarg, &sin_out.sin_addr);
            break;
        case 'p':
            sin_out.sin_port = htons(atoi(optarg));
            break;
        case 't':
            send_time = atoi(optarg);
            break;
        case 'x':
            axes_count = atoi(optarg);
            break;
        case 'h':
            printf("rcjoystick by whoim@mail.ru\ncapture usb-hid joystic state and share to mavlink reciever as RC_CHANNELS_OVERRIDE packets\nUsage:\n [-v] verbose;\n [-d device] default '/dev/input/js0';\n [-a addr] ip address send to, default 127.0.0.1;\n [-p port] udp port send to, default 14650;\n [-t time] update RC_CHANNEL_OVERRIDE time in ms, default 50;\n [-x axes_count] 2..9 axes, default 5, other channels mapping to js buttons from button 0;\n");
            return 0;
        }
  }

  while (true) { //loop
    js = open(device, O_RDONLY | O_NONBLOCK);

    if (js == -1) {
        perror("Could not open joystick");
        //close(js);
        perror ("open()");
        sleep (1); //try again later
        continue; //go to start while true
        //return 0;
    }

    printf("Device: %s, %d axes, %d buttons\n", device, get_axes_count(js), get_button_count(js));
    printf("Update time: %dms\n", send_time);
    printf("UDP: %s:%d\n", inet_ntoa(sin_out.sin_addr), ntohs(sin_out.sin_port));
    printf("Used axes: %d, other channels as buttons\n", axes_count);
    printf("Started\n");
    
    do //while not errno read js
    {
     errsv = 11; //reset
     if( read_event(js, &event) == 0 ) {
         switch (event.type)
            {
                case JS_EVENT_BUTTON:
                    if (verbose) printf("Button %u %s\n", event.number, event.value ? "pressed" : "released");
                    buttons[event.number] = event.value ? 2000 : 1000;
                    break;
                case JS_EVENT_AXIS:
                    axis = get_axis_state(&event, axes);
                    if (axis < axes_count)
                        if (verbose) printf("Axis %zu at (%6d, %6d)\n", axis, axes[axis].x, axes[axis].y);
                    break;
                default:
                    // Ignore init events.
                    break;
            }
        }
        //send to udp
        if( (long long)time_check + send_time < millis() ){
            uint8_t btnidx = 0;
            mavlink_msg_rc_channels_override_pack(255, 190, &msg, 0, 0,
                    axes_to_ch(axes[0].x), //ch1
                    axes_to_ch(axes[0].y), //ch2
                    axes_to_ch(axes[1].x), //ch3
                    axes_to_ch(axes[1].y), //ch4
                    
                    (axes_count > 2) ? axes_to_ch(axes[2].x) : buttons[btnidx],   //ch5
                    (axes_count > 2) ? axes_to_ch(axes[2].y) : buttons[btnidx++], //ch6
                    (axes_count > 3) ? axes_to_ch(axes[3].x) : buttons[btnidx++], //ch7
                    (axes_count > 3) ? axes_to_ch(axes[3].y) : buttons[btnidx++], //ch8
                    (axes_count > 4) ? axes_to_ch(axes[4].x) : buttons[btnidx++], //ch9
                    (axes_count > 4) ? axes_to_ch(axes[4].y) : buttons[btnidx++], //ch10
                    (axes_count > 5) ? axes_to_ch(axes[5].x) : buttons[btnidx++], //ch11
                    (axes_count > 5) ? axes_to_ch(axes[5].y) : buttons[btnidx++], //ch12
                    (axes_count > 6) ? axes_to_ch(axes[6].x) : buttons[btnidx++], //ch13
                    (axes_count > 6) ? axes_to_ch(axes[6].y) : buttons[btnidx++], //ch14
                    (axes_count > 7) ? axes_to_ch(axes[7].x) : buttons[btnidx++], //ch15
                    (axes_count > 7) ? axes_to_ch(axes[7].y) : buttons[btnidx++], //ch16
                    (axes_count > 8) ? axes_to_ch(axes[8].x) : buttons[btnidx++], //ch17
                    (axes_count > 8) ? axes_to_ch(axes[8].y) : buttons[btnidx++]  //ch18
             );
            len = mavlink_msg_to_send_buffer(buf, &msg);
            bytes_sent = sendto(out_sock, buf, len, 0, (struct sockaddr *)&sin_out, sizeof(sin_out));
            if (verbose) printf("Sent %d bytes\n", bytes_sent);
            time_check = millis();
        }
        if (verbose) fflush(stdout);
        nanosleep(&tw, &tr); //for low CPU ut
    } while (errsv == 11 || errsv == 38); //while not err
  } //while true
}
