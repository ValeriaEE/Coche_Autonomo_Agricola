/*
 * Chasis+ActuadoresySensores.c
 *
 * Created: 03/06/2026 04:37:01 p. m.
 * Author: Valeria Escalante
 *
 * --- PROTOCOLO DE COMUNICACIÓN UART (bidireccional) ---
 *
 * ATmega ? Raspberry Pi (TX, PD1):
 *   "CSTART\n"  ? El robot está listo y esperando comando RUN
 *   "C05S\n"    ? Cuadrante 5, Seco (necesita riego)
 *   "C12H\n"    ? Cuadrante 12, Húmedo (OK)
 *   "CPAUSE\n"  ? Confirma que el robot se pausó
 *   "CSTOP\n"   ? Confirma que el robot se detuvo
 *   "CFIN\n"    ? Mapeo completo terminado
 *
 * Raspberry Pi ? ATmega (RX, PD0):
 *   'R' ? Run   — Iniciar o reanudar el ciclo
 *   'P' ? Pause — Frenar en seco y esperar
 *   'S' ? Stop  — Detener y reiniciar todas las variables
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

// --- CONFIGURACIÓN DE TIEMPOS Y UMBRALES ---
#define TIEMPO_10CM    1200  // Recuerda calibrarlo
#define TIEMPO_GIRO_90 800   // Recuerda calibrarlo
#define UMBRAL_SECO    600   // Límite para detectar tierra seca (0-1023)

// Configuración de Voltaje de Referencia para el ADC
#define ADC_VREF_TYPE ((0<<REFS1) | (1<<REFS0) | (0<<ADLAR))

// Configuración de UART a 9600 baudios con reloj de 1 MHz
// UBRR = (F_CPU / (16 * BAUD)) - 1 = (1000000 / (16 * 9600)) - 1 ˜ 5
#define UART_UBRR_VAL 5

// --- ESTADOS DEL ROBOT ---
// Se usan como valores de la variable global "estado_robot"
#define ESTADO_ESPERANDO 0  // Esperando comando RUN inicial
#define ESTADO_CORRIENDO 1  // Ciclo en marcha
#define ESTADO_PAUSADO   2  // Pausado, esperando reanudar
#define ESTADO_DETENIDO  3  // Stop total, variables reiniciadas

// --- VARIABLES DE CONTROL ---
unsigned char cuadrante_actual = 0;
unsigned char fila_actual      = 0;
unsigned int  valor_humedad    = 0;
char buffer_lcd[16];
char buffer_uart[12];

// Variable de estado: la ISR la modifica, el loop principal la lee.
// "volatile" le dice al compilador que esta variable puede cambiar
// en cualquier momento (por la interrupción) y que no la optimice.
volatile unsigned char estado_robot = ESTADO_ESPERANDO;


// ============================================================
// --- INTERRUPCIÓN DE RECEPCIÓN UART (ISR) ---
// Se ejecuta automáticamente cada vez que llega un byte por RX.
// Debe ser corta: solo lee el byte y actualiza el estado.
// ============================================================

interrupt [USART_RXC] void uart_rx_isr(void)
{
    unsigned char comando = UDR0; // Leer el byte recibido (limpia la bandera)

    switch (comando)
    {
        case 'R': // Run: solo activa si estaba pausado o esperando
            if (estado_robot == ESTADO_PAUSADO ||
                estado_robot == ESTADO_ESPERANDO)
            {
                estado_robot = ESTADO_CORRIENDO;
            }
            break;

        case 'P': // Pause: solo activa si estaba corriendo
            if (estado_robot == ESTADO_CORRIENDO)
            {
                estado_robot = ESTADO_PAUSADO;
            }
            break;

        case 'S': // Stop: activa desde cualquier estado
            estado_robot = ESTADO_DETENIDO;
            break;

        default:
            break; // Ignora bytes desconocidos
    }
}


// ============================================================
// --- FUNCIONES UART ---
// ============================================================

void uart_init(void)
{
    // Configura el baud rate
    UBRR0H = (unsigned char)(UART_UBRR_VAL >> 8);
    UBRR0L = (unsigned char)(UART_UBRR_VAL);

    // Habilita transmisor (TX) y receptor (RX),
    // y activa la interrupción de recepción (RXCIE0)
    UCSR0B = (1<<RXCIE0) | (1<<RXEN0) | (1<<TXEN0);

    // Formato de frame: 8 bits de datos, 1 bit de stop, sin paridad
    UCSR0C = (1<<UCSZ01) | (1<<UCSZ00);
}

void uart_send_byte(unsigned char data)
{
    while (!(UCSR0A & (1<<UDRE0)));
    UDR0 = data;
}

void uart_send_string(char *str)
{
    while (*str)
    {
        uart_send_byte(*str);
        str++;
    }
}

// Envía el estado de un cuadrante a la Raspberry Pi
// Formato: "C[##][H/S]\n"  Ejemplo: "C05S\n", "C36H\n"
void uart_enviar_estado(unsigned char cuadrante, unsigned char humedo)
{
    sprintf(buffer_uart, "C%02dX\n", cuadrante);
    buffer_uart[3] = humedo ? 'H' : 'S';
    uart_send_string(buffer_uart);
}


// ============================================================
// --- FUNCIÓN DE ESPERA SEGURA ---
// Reemplaza delay_ms() en los lugares donde el robot se mueve.
// Espera el tiempo indicado pero interrumpe si llega PAUSE o STOP.
// Devuelve 1 si se completó el tiempo, 0 si fue interrumpido.
// ============================================================

unsigned char esperar_ms(unsigned int ms)
{
    unsigned int i;
    for (i = 0; i < ms; i++)
    {
        // Revisa el estado cada 1ms
        if (estado_robot == ESTADO_PAUSADO ||
            estado_robot == ESTADO_DETENIDO)
        {
            frenar(); // Frena inmediatamente
            return 0; // Indica que fue interrumpido
        }
        delay_ms(1);
    }
    return 1; // Indica que se completó normalmente
}


// ============================================================
// --- LECTURA DEL SENSOR DE HUMEDAD (Pin PC0 / A0) ---
// ============================================================

unsigned int read_adc(unsigned char adc_input)
{
    ADMUX = adc_input | ADC_VREF_TYPE;
    delay_us(10);
    ADCSRA |= (1<<ADSC);
    while ((ADCSRA & (1<<ADIF))==0);
    ADCSRA |= (1<<ADIF);
    return ADCW;
}


// ============================================================
// --- CONTROL DE SERVO POR SOFTWARE (Pin PB5) ---
// ============================================================

void servo_arriba(void)
{
    unsigned char i;
    for(i=0; i<40; i++)
    {
        PORTB.5 = 1;
        delay_us(1000);
        PORTB.5 = 0;
        delay_us(19000);
    }
}

void servo_abajo(void)
{
    unsigned char i;
    for(i=0; i<40; i++)
    {
        PORTB.5= 1;
        delay_us(2000);
        PORTB.5 = 0;
        delay_us(18000);
    }
}


// ============================================================
// --- FUNCIONES DE MOVIMIENTO ---
// ============================================================

void frenar(void)
{
     // Apaga Lado Izquierdo (PB1 y PB3)
    PORTB.1 = 0; PORTB.3 = 0; 
    
    // Apaga Lado Derecho (PB2 y PB4)
    PORTB.2 = 0; PORTB.4 = 0; 
}

void avanzar(void)
{
    frenar();  
    
    // Lado Izquierdo Adelante
    PORTB.1 = 1; PORTB.3 = 0; 
    
    // Lado Derecho Adelante
    PORTB.2 = 1; PORTB.4 = 0;  
}

void girar_derecha(void)
{
    frenar();
    // Lado Izquierdo Adelante
    PORTB.1 = 1; PORTB.3 = 0;
    
    // Lado Derecho Atrás (Giro tipo tanque)
    PORTB.2 = 0; PORTB.4 = 1;
}

void girar_izquierda(void)
{
    frenar();
    // Lado Izquierdo Atrás (Giro tipo tanque)
    PORTB.1 = 0; PORTB.3 = 1;
    
    // Lado Derecho Adelante
    PORTB.2 = 1; PORTB.4 = 0;
}

// --- MANIOBRA DE VUELTA EN U ---
void realizar_giro_en_u(void)
{
    MoveCursor(0,0);
    StringLCD("GIRANDO EN U... ");
    
    if (fila_actual % 2 != 0) 
    {
        girar_derecha();   
        delay_ms(TIEMPO_GIRO_90); 
        frenar();
        delay_ms(500);
        avanzar();        
        delay_ms(TIEMPO_10CM);   
        frenar(); 
        delay_ms(500);
        girar_derecha();   
        delay_ms(TIEMPO_GIRO_90); 
        frenar(); 
        delay_ms(500); 
    } 
    else 
    {
        girar_izquierda(); 
        delay_ms(TIEMPO_GIRO_90);
        frenar(); 
        delay_ms(500);
        avanzar();         
        delay_ms(TIEMPO_10CM);
        frenar(); 
        delay_ms(500);
        girar_izquierda(); 
        delay_ms(TIEMPO_GIRO_90);}
        frenar(); 
        delay_ms(500);
    }
    frenar();
    delay_ms(500); 
}

// ============================================================
// --- FUNCIÓN: REINICIAR VARIABLES DE RECORRIDO ---
// Se llama cuando llega un comando STOP
// ============================================================

void reiniciar_recorrido(void)
{
    frenar();
    servo_arriba();
    cuadrante_actual = 1;
    fila_actual      = 1;
    valor_humedad    = 0;

    uart_send_string("CSTOP\n"); // Notifica a la Raspberry Pi

    MoveCursor(0,0);
    StringLCD("DETENIDO        ");
    MoveCursor(0,1);
    StringLCD("ESPERANDO START ");
}


// ============================================================
// --- PROGRAMA PRINCIPAL ---
// ============================================================

void main(void)
{
    // --- INICIALIZACIÓN DE ADC ---
    ADCSRA = (1<<ADEN)  | (0<<ADSC)  | (0<<ADATE) | (0<<ADIF) |
             (0<<ADIE)  | (0<<ADPS2) | (1<<ADPS1) | (1<<ADPS0);
    ADCSRB = (0<<ADTS2) | (0<<ADTS1) | (0<<ADTS0);
    DIDR0  = (1<<ADC0D);

    // --- INICIALIZACIÓN DE UART ---
    uart_init();

    // --- CONFIGURACIÓN DE SALIDAS DIGITALES ---
    SetupLCD();

    DDRB.1 = 1; //IN1 Lado izquierdo 
    DDRB.3 = 1; //IN2 Lado izquierdo
    DDRB.2 = 1; //IN1 Lado derecho
    DDRB.4 = 1; //IN2 Lado derecho 
    DDRD.1 = 1; // TX como salida
    DDRD.0 = 0; // RX como entrada 
    DDRB.5 = 1; //Servo como salida 

    // --- HABILITAR INTERRUPCIONES GLOBALES ---
    // Necesario para que la ISR de UART funcione
    #asm("sei")

    frenar();
    servo_arriba();

    // --- PANTALLA DE BIENVENIDA ---
    MoveCursor(0,0);
    StringLCD("SISTEMA LISTO   ");
    MoveCursor(0,1);
    StringLCD("ESPERANDO START ");

    // Notifica a la Raspberry Pi que el sistema está listo
    uart_send_string("CSTART\n");

    // --- ESPERA EL COMANDO 'R' INICIAL ANTES DE ARRANCAR ---
    while (estado_robot != ESTADO_CORRIENDO);

    // ============================================================
    // --- LOOP PRINCIPAL ---
    // ============================================================

    while (1)
    {
        // --- BLOQUE DE PAUSA ---
        // Si en cualquier punto del loop se recibe PAUSE,
        // el robot ya frenó (en esperar_ms o en la ISR).
        // Aquí simplemente espera hasta recibir RUN de nuevo.
        if (estado_robot == ESTADO_PAUSADO)
        {
            uart_send_string("CPAUSE\n");
            MoveCursor(0,0);
            StringLCD("** PAUSADO **   ");
            MoveCursor(0,1);
            StringLCD("ESPERA START    ");

            while (estado_robot == ESTADO_PAUSADO); // Espera activa

            // Si al salir de la pausa llega STOP, lo maneja abajo
        }

        // --- BLOQUE DE STOP ---
        // Reinicia todo y vuelve a esperar comando RUN
        if (estado_robot == ESTADO_DETENIDO)
        {
            reiniciar_recorrido();
            estado_robot = ESTADO_ESPERANDO;

            while (estado_robot != ESTADO_CORRIENDO); // Espera RUN
        }

        // --- LÓGICA DE RECORRIDO ---
        if (cuadrante_actual <= 36)
        {
            // 1. Mostrar cuadrante y avanzar
            MoveCursor(0,0);
            StringLCD("AVANZANDO...    ");
            MoveCursor(0,1);
            sprintf(buffer_lcd, "Cuadrante: %2d   ", cuadrante_actual);
            StringLCD(buffer_lcd);

            avanzar();
            if (!esperar_ms(TIEMPO_10CM)) continue; // Si se interrumpe, vuelve al inicio del loop
            frenar();

            // 2. Bajar sensor y medir
            MoveCursor(0,0);
            StringLCD("MIDIENDO TIERRA ");

            servo_abajo();
            if (!esperar_ms(500)) continue;

            valor_humedad = read_adc(0);

            // 3. Mostrar resultado y enviar a Raspberry Pi
            MoveCursor(0,1);
            if (valor_humedad > UMBRAL_SECO)
            {
                StringLCD("SECO - REGAR!   ");
                uart_enviar_estado(cuadrante_actual, 0);
            }
            else
            {
                StringLCD("HUMEDO - OK     ");
                uart_enviar_estado(cuadrante_actual, 1);
            }

            if (!esperar_ms(2000)) continue;

            // 4. Subir sensor
            servo_arriba();

            // 5. Girar en U al final de cada fila
            if (cuadrante_actual % 6 == 0)
            {
                if (cuadrante_actual < 36)
                {
                    realizar_giro_en_u();
                    fila_actual++;
                }
            }

            cuadrante_actual++;
        }
        else
        {
            // --- FIN DEL MAPEO ---
            frenar();
            uart_send_string("CFIN\n");

            MoveCursor(0,0);
            StringLCD("MAPEO COMPLETO  ");
            MoveCursor(0,1);
            StringLCD("FIN DEL VIAJE   ");
            while(1);
        }
    }
}