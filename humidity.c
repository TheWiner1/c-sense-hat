#include <fcntl.h>
#include <i2c/smbus.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "humidity.h"



// int main(){
//     double T_DegC, H_rH; 
//     getHumidity(&T_DegC, &H_rH);

//     printf("Temp (from humid) = %.1f°C\n", T_DegC);
//     printf("Humidity = %.0f%% rH\n", H_rH);
    
//     return 0;
// }



void getHumidity(double *T_DegC, double *H_rH) {
    int fd = 0;
    uint8_t status = 0;

    /* open i2c comms */
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) {
        perror("Unable to open i2c device");
        exit(1);
    }

    /* configure i2c slave */
    if (ioctl(fd, I2C_SLAVE, DEV_ID) < 0) {
        perror("Unable to configure i2c slave device");
        close(fd);
        exit(1);
    }

    /* check we are who we should be */
    if (i2c_smbus_read_byte_data(fd, WHO_AM_I) != 0xBC) {
        printf("%s\n", "who_am_i error");
        close(fd);
        exit(1);
    }

    /* Power down the device (clean start) */
    i2c_smbus_write_byte_data(fd, CTRL_REG1, 0x00);

    /* Turn on the humidity sensor analog front end in single shot mode  */
    i2c_smbus_write_byte_data(fd, CTRL_REG1, 0x84);

    /* Run one-shot measurement (temperature and humidity). The set bit will be reset by the
     * sensor itself after execution (self-clearing bit) */
    i2c_smbus_write_byte_data(fd, CTRL_REG2, 0x01);

    /* Wait until the measurement is completed */
    do {
        delay(25); /* 25 milliseconds */
        status = i2c_smbus_read_byte_data(fd, CTRL_REG2);
    } while (status != 0);

    /* Read calibration temperature LSB (ADC) data
     * (temperature calibration x-data for two points)
     */
    uint8_t t0_out_l = i2c_smbus_read_byte_data(fd, T0_OUT_L);
    uint8_t t0_out_h = i2c_smbus_read_byte_data(fd, T0_OUT_H);
    uint8_t t1_out_l = i2c_smbus_read_byte_data(fd, T1_OUT_L);
    uint8_t t1_out_h = i2c_smbus_read_byte_data(fd, T1_OUT_H);

    /* Read calibration temperature (°C) data
     * (temperature calibration y-data for two points)
     */
    uint8_t t0_degC_x8 = i2c_smbus_read_byte_data(fd, T0_degC_x8);
    uint8_t t1_degC_x8 = i2c_smbus_read_byte_data(fd, T1_degC_x8);
    uint8_t t1_t0_msb = i2c_smbus_read_byte_data(fd, T1_T0_MSB);

    /* Read calibration relative humidity LSB (ADC) data
     * (humidity calibration x-data for two points)
     */
    uint8_t h0_out_l = i2c_smbus_read_byte_data(fd, H0_T0_OUT_L);
    uint8_t h0_out_h = i2c_smbus_read_byte_data(fd, H0_T0_OUT_H);
    uint8_t h1_out_l = i2c_smbus_read_byte_data(fd, H1_T0_OUT_L);
    uint8_t h1_out_h = i2c_smbus_read_byte_data(fd, H1_T0_OUT_H);

    /* Read relative humidity (% rH) data
     * (humidity calibration y-data for two points)
     */
    uint8_t h0_rh_x2 = i2c_smbus_read_byte_data(fd, H0_rH_x2);
    uint8_t h1_rh_x2 = i2c_smbus_read_byte_data(fd, H1_rH_x2);

    /* make 16 bit values (bit shift)
     * (temperature calibration x-values)
     */
    int16_t T0_OUT = t0_out_h << 8 | t0_out_l;
    int16_t T1_OUT = t1_out_h << 8 | t1_out_l;

    /* make 16 bit values (bit shift)
     * (humidity calibration x-values)
     */
    int16_t H0_T0_OUT = h0_out_h << 8 | h0_out_l;
    int16_t H1_T0_OUT = h1_out_h << 8 | h1_out_l;

    /* make 16 and 10 bit values (bit mask and bit shift) */
    uint16_t T0_DegC_x8 = (t1_t0_msb & 3) << 8 | t0_degC_x8;
    uint16_t T1_DegC_x8 = ((t1_t0_msb & 12) >> 2) << 8 | t1_degC_x8;

    /* Calculate calibration values
     * (temperature calibration y-values)
     */
    double T0_DegC = T0_DegC_x8 / 8.0;
    double T1_DegC = T1_DegC_x8 / 8.0;

    /* Humidity calibration values
     * (humidity calibration y-values)
     */
    double H0_rH = h0_rh_x2 / 2.0;
    double H1_rH = h1_rh_x2 / 2.0;

    /* Solve the linear equasions 'y = mx + c' to give the
     * calibration straight line graphs for temperature and humidity
     */
    double t_gradient_m = (T1_DegC - T0_DegC) / (T1_OUT - T0_OUT);
    double t_intercept_c = T1_DegC - (t_gradient_m * T1_OUT);

    double h_gradient_m = (H1_rH - H0_rH) / (H1_T0_OUT - H0_T0_OUT);
    double h_intercept_c = H1_rH - (h_gradient_m * H1_T0_OUT);

    /* Read the ambient temperature measurement (2 bytes to read) */
    uint8_t t_out_l = i2c_smbus_read_byte_data(fd, TEMP_OUT_L);
    uint8_t t_out_h = i2c_smbus_read_byte_data(fd, TEMP_OUT_H);

    /* make 16 bit value */
    int16_t T_OUT = t_out_h << 8 | t_out_l;

    /* Read the ambient humidity measurement (2 bytes to read) */
    uint8_t h_t_out_l = i2c_smbus_read_byte_data(fd, H_T_OUT_L);
    uint8_t h_t_out_h = i2c_smbus_read_byte_data(fd, H_T_OUT_H);

    /* make 16 bit value */
    int16_t H_T_OUT = h_t_out_h << 8 | h_t_out_l;

    /* Calculate ambient temperature */
    *T_DegC = (t_gradient_m * T_OUT) + t_intercept_c;

    /* Calculate ambient humidity */
    *H_rH = (h_gradient_m * H_T_OUT) + h_intercept_c;

    /* Output */
    // printf("Temp (from humid) = %.1f°C\n", T_DegC);
    // printf("Humidity = %.0f%% rH\n", H_rH);

    /* Power down the device */
    i2c_smbus_write_byte_data(fd, CTRL_REG1, 0x00);
    close(fd);

    // return (0);
}

void delay(int t) {
    usleep(t * 1000);
}
