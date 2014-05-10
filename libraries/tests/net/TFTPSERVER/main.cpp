// Simple TFTP Server based on UDP echo server
// files are written to internal /local/ storage
#include "mbed.h"
#include "cmsis_os.h"
#include "EthernetInterface.h"

const int TFTP_SERVER_PORT = 69;
const int BUFFER_SIZE = 592;
enum TFTPState { idle, receiving } tftpState = idle;
DigitalOut led1(LED1);
DigitalOut led2(LED2);

void tftpthread(void const *args);
osThreadDef(tftpthread, osPriorityNormal, DEFAULT_STACK_SIZE);

LocalFileSystem local("local");   //File System
EthernetInterface eth;
UDPSocket server;

int main (void) {

    eth.init(); //Use DHCP
    eth.connect();
    server.bind(TFTP_SERVER_PORT);
    printf("TFTP Server listening on IP Address %s port %d\n\r", eth.getIPAddress(), TFTP_SERVER_PORT);

    osThreadCreate(osThread(tftpthread), NULL);
    while (true) {
        led1 = !led1;
        osDelay(500);
    }
}

void tftpthread(void const *args) {
    extern LocalFileSystem local;
    extern UDPSocket server;
    extern DigitalOut led2;
    extern TFTPState tftpState;
    char buffer[BUFFER_SIZE] = {0};
    char ack[4]; ack[0] = 0x00; ack[1] = 0x04; ack[2] = 0x00; ack[3] = 0x00;
    char filename[32], mode[6], errmsg[32];
    unsigned int cnt = 0;
    Endpoint client;
    FILE *fp = NULL;
    short int timeout = 0;

    server.set_blocking(false,1); // Set non-blocking
    while (true) {
        int n = server.receiveFrom(client, buffer, sizeof(buffer));
        if (n > 0) {
            if (cnt>603235) cnt = 0;    // package count is only 2 bytes in TFTP
            ack[2] = cnt >> 8;          // store current count in ACK for sending
            ack[3] = cnt & 255;         //
            if (tftpState == idle) {
                if (buffer[1] == 0x02) {    // 0x02 means "request to write file"
                    printf("Received WRQ, %d bytes\n\r", n);
                    snprintf(filename, 32, "/local/%s", &buffer[2]); // filename starts at byte 2
                    snprintf(mode, 6, "%s", &buffer[3+strlen(filename)-7]); // mode is after filename
                    printf("Received WRQ for file %s, mode %s.\n\r", filename, mode);
                    if (strcmp(mode, "octet") == 0) { // we only support octet/binary mode!
                        fp = fopen(filename, "wb");
                        if (fp == NULL) {
                            printf("Error opening file %s\n\r", filename);
                            snprintf(errmsg, 32, "%c%c%c%cError opening file %s",ack[0], 0x05, ack[2], ack[3], filename);
                            server.sendTo(client, errmsg, strlen(errmsg));
                            led2 = !led2;
                        }
                        else {
                            tftpState = receiving;
                            ack[1] = 0x04;
                            server.sendTo(client, ack, 4);
                            led2 = !led2;
                            cnt++;
                            timeout=0;
                        }
                    } else {
                        printf("No octet\n\r");
                        snprintf(errmsg, 32, "%c%c%c%cOnly octet files accepted",ack[0], 0x05, ack[2], ack[3]);
                        server.sendTo(client, errmsg, strlen(errmsg));
                        led2 = !led2;
                    }
                }
            } else if (tftpState == receiving) {
                if(buffer[1] == 0x03) {
                    if ((buffer[2] == ack[2]) && (buffer[3] == ack[3])) {
                        fwrite(&buffer[4], 1, n-4, fp);
                        server.sendTo(client,ack, 4);
                        led2 = !led2;
                        cnt++;
                        timeout = 0;
                        if (n < 516) {
                            printf("Received %d packages\n\r", cnt+1);
                            fclose(fp);
                            tftpState = idle;
                            cnt = 0;
                        }
                    } else {
                        printf("Order mismatch\n\r");
                        snprintf(errmsg, 32, "%c%c%c%cPacket order mismatch",ack[0], 0x05, ack[2], ack[3]);
                        server.sendTo(client, errmsg, strlen(errmsg));
                        led2 = !led2;
                    }
                } else {
                    printf("Unexpected packet type: %d\n\r",buffer[1]);
                    snprintf(errmsg, 32, "%c%c%c%cUnexpected packet type %d",ack[0], 0x05, ack[2], ack[3], buffer[1]);
                    server.sendTo(client, errmsg, strlen(errmsg));
                    led2 = !led2;
                }
            }
        } else {
            if (timeout>10) {
                if (tftpState == receiving) {
                    printf("Timeout!\n\r");
                    fclose(fp);
                    // removefile(filename);
                    tftpState = idle;
                } else {
                    timeout = 0;
                    led2 = !led2;
                }
            }
            timeout++;
            osDelay(500);
        }
    }
}


