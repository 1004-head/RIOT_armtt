/*
 * Copyright (C) 2014 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       Hello World application
 *
 * @author      Kaspar Schleiser <kaspar@schleiser.de>
 * @author      Ludwig Knüpfer <ludwig.knuepfer@fu-berlin.de>
 *
 * @}
 */

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    puts("Hello malloc!");

    printf("You are running RIOT on a(n) %s board.\n", RIOT_BOARD);
    printf("This board features a(n) %s MCU.\n", RIOT_MCU);

    char *data = (char *)malloc(256);
    char *data1 = (char *)malloc(258);
    char *data2 = (char *)malloc(260);
    printf("data ptr : %p\n", data);
    printf("data1 ptr : %p\n", data1);
    printf("data2 ptr : %p\n", data2);
    if(data){
      snprintf(data, 10, "Hello\n");

      printf("%s\n", data);

      free(data);
    }
    char *data3 = (char *)malloc(256);
    printf("data3 ptr : %p\n", data3);

    free(data1);
    free(data2);
    free(data3);

    return 0;
}
