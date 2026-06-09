/*
 * Chasis+ActuadoresySensores.c
 *
 * Created: 03/06/2026 04:37:01 p. m.
 * Author: Valeria Escalante
 * 
 */
 
#asm
    .equ __lcd_port=0x08    ; LCD asignada por completo al PORTC
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
#define TIEMPO_GIRO_90 800   // Recuerda Calibrarlo 
#define UMBRAL_SECO    600   // Límite para detectar tierra seca (0-1023)

// Configuración de Voltaje de Referencia para el ADC 
#define ADC_VREF_TYPE ((0<<REFS1) | (1<<REFS0) | (0<<ADLAR))

// --- VARIABLES DE CONTROL ---
unsigned char cuadrante_actual = 1;
unsigned char fila_actual = 1;
unsigned int  valor_humedad = 0;
char buffer_lcd[16];         

// --- LECTURA DEL SENSOR DE HUMEDAD (Pin PC0 / A0) ---
unsigned int read_adc(unsigned char adc_input)
//La funcion devulve un entero y recibe adc_input que es ADC0
{
    ADMUX = adc_input | ADC_VREF_TYPE;   // Qué pin leer y qué voltaje usar 
    delay_us(10);                        // Pausa para estabilizar 
    ADCSRA |= (1<<ADSC);                 // Empieza a procesar 
    while ((ADCSRA & (1<<ADIF))==0);     // Espera hasta que esté procesado 
    ADCSRA |= (1<<ADIF);                 // Listo para la siguiente lectura 
    return ADCW;                         // Devuelve resolución completa de 10 bits
}

// --- CONTROL DE SERVO POR SOFTWARE (Pin PB4) ---
void servo_arriba(void)
{
    unsigned char i;
    for(i=0; i<40; i++) {    
        PORTB.4 = 1; 
        delay_us(1000); // Pulso de 1ms (0 grados)
        PORTB.4 = 0; 
        delay_ms(19);   // Completa el ciclo de 20ms
    }
}

void servo_abajo(void)
{
    unsigned char i;
    for(i=0; i<40; i++) {   
        PORTB.4 = 1; 
        delay_us(2000); // Pulso de 2ms (180 grados)
        PORTB.4 = 0; 
        delay_ms(18);   //delay para completar el ancho del pulso 
    }
}

// --- FUNCIONES DE MOVIMIENTO  ---
void frenar(void)
{
    // Apaga Lado Izquierdo (PB1 y PB2)
    PORTB.1 = 0; PORTB.2 = 0; 
    
    // Apaga Lado Derecho (PB3 y PD3)
    PORTB.3 = 0; PORTD.3 = 0; 
}

void avanzar(void)
{
    frenar();  
    
    // Lado Izquierdo Adelante
    PORTB.1 = 1; PORTB.2 = 0; 
    
    // Lado Derecho Adelante
    PORTB.3 = 1; PORTD.3 = 0;  
}

void girar_derecha(void)
{
    frenar();
    // Lado Izquierdo Adelante
    PORTB.1 = 1; PORTB.2 = 0;
    
    // Lado Derecho Atrás (Giro tipo tanque)
    PORTB.3 = 0; PORTD.3 = 1;
}

void girar_izquierda(void)
{
    frenar();
    // Lado Izquierdo Atrás (Giro tipo tanque)
    PORTB.1 = 0; PORTB.2 = 1;
    
    // Lado Derecho Adelante
    PORTB.3 = 1; PORTD.3 = 0;
}

// --- MANIOBRA DE VUELTA EN U ---
void realizar_giro_en_u(void)
{
    MoveCursor(0,0);
    StringLCD("GIRANDO EN U... ");
    
    if (fila_actual % 2 != 0) 
    {
        girar_derecha();   delay_ms(TIEMPO_GIRO_90);
        avanzar();         delay_ms(TIEMPO_10CM);
        girar_derecha();   delay_ms(TIEMPO_GIRO_90);
    } 
    else 
    {
        girar_izquierda(); delay_ms(TIEMPO_GIRO_90);
        avanzar();         delay_ms(TIEMPO_10CM);
        girar_izquierda(); delay_ms(TIEMPO_GIRO_90);
    }
    frenar();
    delay_ms(500); 
}

void main(void)
{
    // --- INICIALIZACIÓN DE ADC PARA RELOJ DE 1 MHz ---
    ADCSRA=(1<<ADEN) | (0<<ADSC) | (0<<ADATE) | (0<<ADIF) | (0<<ADIE) | (0<<ADPS2) | (1<<ADPS1) | (1<<ADPS0);
    ADCSRB=(0<<ADTS2) | (0<<ADTS1) | (0<<ADTS0);
    DIDR0=(1<<ADC0D); // Desactiva entrada digital en PC0 (A0) para evitar ruido

    // --- CONFIGURACIÓN DE SALIDAS DIGITALES ---
    SetupLCD();
    
    // Configurar pines de motores como salidas (DDR = 1)
    DDRB.1 = 1; DDRB.2 = 1; // Lado Izquierdo (PB1 y PB2)
    DDRB.3 = 1; DDRD.3 = 1; // Lado Derecho (PD3 y PB3)
    
    // Configurar pin del Servo como salida
    DDRB.4 = 1;             // Servo (PB4)
    
    frenar();
    servo_arriba(); // Iniciar con el brazo del sensor arriba para no arrastrarlo
    
    MoveCursor(0,0);
    StringLCD("SISTEMA LISTO   ");
    delay_ms(3000); // 3 segundos de tolerancia para colocar el carro en el suelo

    while (1)
    {
        if (cuadrante_actual <= 36)
        {
            // 1. Desplegar cuadrante actual e iniciar avance
            MoveCursor(0,0);
            StringLCD("AVANZANDO...    ");
            MoveCursor(0,1);
            sprintf(buffer_lcd, "Cuadrante: %2d   ", cuadrante_actual);
            StringLCDVar(buffer_lcd);
            
            avanzar();
            delay_ms(TIEMPO_10CM);
            frenar();
            
            // 2. Proceso mecánico de medición
            MoveCursor(0,0);
            StringLCD("MIDIENDO TIERRA ");
            
            servo_abajo();  // Introduce el sensor en la tierra
            delay_ms(500);  // Pausa para estabilización física
            
            valor_humedad = read_adc(0); // Lectura del pin analógico A0 (PC0)
            
            // 3. Analizar datos del suelo
            MoveCursor(0,1);
            if (valor_humedad > UMBRAL_SECO) {
                StringLCD("SECO - REGAR!   ");
            } else {
                StringLCD("HUMEDO - OK     ");
            }
            
            delay_ms(2000); // Mantener el mensaje en pantalla por 2 segundos
            
            servo_arriba(); // Retraer el sensor de forma segura antes de avanzar de nuevo
            
            // 4. Verificación de final de fila (Múltiplos de 6)
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
            // --- FIN DE LA MATRIZ DE RECORRIDO ---
            frenar();
            MoveCursor(0,0);
            StringLCD("MAPEO COMPLETO  ");
            MoveCursor(0,1);
            StringLCD("FIN DEL VIAJE   ");
            while(1); // Bucle de bloqueo final
        }
    }
}