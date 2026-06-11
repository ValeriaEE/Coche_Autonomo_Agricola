/*
 * UARTPrueba.c
 *
 * Created: 10/06/2026 01:30:04 p. m.
 * Author: Usuario
 */
/*
 * TEST_LCD_UART.c
 *
 * Prueba de LCD y comunicación UART con Raspberry Pi.
 * NO mueve motores ni servo.
 *
 * Qué hace:
 *  - Muestra mensajes en el LCD
 *  - Envía el protocolo completo (CSTART, C01H...C36S, CFIN)
 *  - Alterna H y S para simular lecturas reales
 *  - Espera comando 'R' antes de arrancar (igual que el código real)
 */

#asm
    .equ __lcd_port=0x0B    
    .equ __lcd_RS=2
    .equ __lcd_EN=3
    .equ __lcd_D4=4
    .equ __lcd_D5=5
    .equ __lcd_D6=6
    .equ __lcd_D7=7
#endasm

#include <mega328p.h>
#include <display.h>
#include <stdio.h>
#include <delay.h>

#define UART_UBRR_VAL 5

#define ESTADO_ESPERANDO 0
#define ESTADO_CORRIENDO 1
#define ESTADO_PAUSADO   2
#define ESTADO_DETENIDO  3

volatile unsigned char estado_robot = ESTADO_ESPERANDO;

unsigned char cuadrante_actual = 1;
char buffer_lcd[16];
char buffer_uart[12];


// ============================================================
// --- ISR UART ---
// ============================================================

interrupt [USART_RXC] void uart_rx_isr(void)
{
    unsigned char cmd = UDR0;
    switch (cmd)
    {
        case 'R':
            if (estado_robot == ESTADO_ESPERANDO ||
                estado_robot == ESTADO_PAUSADO)
                estado_robot = ESTADO_CORRIENDO;
            break;
        case 'P':
            if (estado_robot == ESTADO_CORRIENDO)
                estado_robot = ESTADO_PAUSADO;
            break;
        case 'S':
            estado_robot = ESTADO_DETENIDO;
            break;
        default:
            break;
    }
}


// ============================================================
// --- UART ---
// ============================================================

void uart_init(void)
{
    UBRR0H = (unsigned char)(UART_UBRR_VAL >> 8);
    UBRR0L = (unsigned char)(UART_UBRR_VAL);
    UCSR0B = (1<<RXCIE0) | (1<<RXEN0) | (1<<TXEN0);
    UCSR0C = (1<<UCSZ01) | (1<<UCSZ00);
}

void uart_send_byte(unsigned char data)
{
    while (!(UCSR0A & (1<<UDRE0)));
    UDR0 = data;
}

void uart_send_string(char *str)
{
    while (*str) { uart_send_byte(*str); str++; }
}

void uart_enviar_estado(unsigned char cuadrante, unsigned char humedo)
{
    sprintf(buffer_uart, "C%02dX\n", cuadrante);
    buffer_uart[3] = humedo ? 'H' : 'S';
    uart_send_string(buffer_uart);
}


// ============================================================
// --- MAIN ---
// ============================================================


unsigned char humedo;

void main(void)
{
    // UART
    uart_init();

    // LCD
    SetupLCD();  
    
     

    // Solo configurar TX y RX — sin DDR de motores ni servo
    DDRD.1 = 1;  // TX salida
    DDRD.0 = 0;  // RX entrada

    // Interrupciones globales
    #asm("sei")

    // Pantalla de bienvenida
    MoveCursor(0,0);
    StringLCD("TEST LCD+UART   ");
    MoveCursor(0,1);
    StringLCD("ESPERANDO RUN...");

    // Notifica a la Raspberry Pi
    uart_send_string("CSTART\n");

    // Espera comando 'R' desde la Raspberry Pi (o puedes
    // puentear PD0 a GND con una resistencia para saltarte esto)
    //while (estado_robot != ESTADO_CORRIENDO);  
    estado_robot = ESTADO_CORRIENDO;

    MoveCursor(0,0);
    StringLCD("INICIANDO...    ");
    delay_ms(1000);

    // -- Ciclo de prueba: simula los 36 cuadrantes -------------

    while (1)
    {
        // Pausa
        if (estado_robot == ESTADO_PAUSADO)
        {
            uart_send_string("CPAUSE\n");
            MoveCursor(0,0);
            StringLCD("** PAUSADO **   ");
            MoveCursor(0,1);
            StringLCD("ESPERA RUN...   ");
            while (estado_robot == ESTADO_PAUSADO);
        }

        // Stop — reinicia el conteo
        if (estado_robot == ESTADO_DETENIDO)
        {
            uart_send_string("CSTOP\n");
            cuadrante_actual = 1;
            estado_robot = ESTADO_ESPERANDO;

            MoveCursor(0,0);
            StringLCD("DETENIDO        ");
            MoveCursor(0,1);
            StringLCD("ESPERANDO RUN...");

            while (estado_robot != ESTADO_CORRIENDO);
        }

        if (cuadrante_actual <= 36)
        {
            // Muestra en LCD qué cuadrante se está "procesando"
            MoveCursor(0,0);
            StringLCD("MIDIENDO...     ");
            MoveCursor(0,1);
            sprintf(buffer_lcd, "Cuadrante: %2d   ", cuadrante_actual);
            StringLCDVar(buffer_lcd);

            // Simula tiempo de medición
            delay_ms(500);

            // Alterna H y S: múltiplos de 3 ? seco, resto ? húmedo
            humedo = (cuadrante_actual % 3 != 0);
            uart_enviar_estado(cuadrante_actual, humedo);

            // Muestra resultado en LCD
            MoveCursor(0,0);
            if (humedo)
                StringLCD("HUMEDO - OK     ");
            else
                StringLCD("SECO - REGAR!   ");

            delay_ms(800);

            cuadrante_actual++;
        }
        else
        {
            // Fin del ciclo
            uart_send_string("CFIN\n");

            MoveCursor(0,0);
            StringLCD("MAPEO COMPLETO  ");
            MoveCursor(0,1);
            StringLCD("FIN DE PRUEBA   ");

            // Espera aquí — si llega STOP reinicia
            while (1)
            {
                if (estado_robot == ESTADO_DETENIDO)
                {
                    cuadrante_actual = 1;
                    estado_robot = ESTADO_ESPERANDO;

                    MoveCursor(0,0);
                    StringLCD("REINICIANDO...  ");
                    delay_ms(1000);

                    uart_send_string("CSTART\n");
                    while (estado_robot != ESTADO_CORRIENDO);
                    break; // Vuelve al while(1) principal
                }
            }
        }
    }
}