/*
 * Chasis.c
 *
 * Created: 03/06/2026 11:38:13 a. m.
 * Author: Valeria Escalante
 * 
 */  
   
 //LCD en Puerto C   
#asm
    .equ __lcd_port=0x08 
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

// --- CONFIGURACIÓN DE TIEMPOS (Calibración) ---
#define TIEMPO_10CM    1200      // Se tiene que cambiar depende de como se mueva 
#define TIEMPO_GIRO_90 800   

// --- VARIABLES DE CONTROL ---
unsigned char cuadrante_actual = 1;
unsigned char fila_actual = 1;
char buffer_lcd[16];         

//FUNCIONES DE MOVIMIENTO 
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
        girar_derecha();   
        delay_ms(TIEMPO_GIRO_90);
        avanzar();        
        delay_ms(TIEMPO_10CM);
        girar_derecha();   
        delay_ms(TIEMPO_GIRO_90);
    } 
    else 
    {
        girar_izquierda(); 
        delay_ms(TIEMPO_GIRO_90);
        avanzar();         
        delay_ms(TIEMPO_10CM);
        girar_izquierda(); 
        delay_ms(TIEMPO_GIRO_90);
    }
    frenar();
    delay_ms(500); 
}

void main(void)
{
    // --- INICIALIZACIONES ---
    SetupLCD();
    
    // Configurar pines de motores como salidas (DDR = 1)
    DDRB.1 = 1; DDRB.2 = 1; // Lado Izquierdo (PB1 y PB2)
    DDRB.3 = 1; DDRD.3 = 1; // Lado Derecho (PB3 y PD3)
    
    frenar();
    
    MoveCursor(0,0);
    StringLCD("SISTEMA LISTO   ");
    delay_ms(3000); 

    while (1)
    {
        // Empieza a avanzar 
        PORTB.1 = 1; PORTB.2 = 0; 
        PORTB.3 = 1; PORTD.3 = 0;     
    
        if (cuadrante_actual <= 36)
        {
            MoveCursor(0,0);
            StringLCD("AVANZANDO...    ");
            MoveCursor(0,1);
            sprintf(buffer_lcd, "Cuadrante: %2d   ", cuadrante_actual);
            StringLCDVar(buffer_lcd);
            
            avanzar();
            delay_ms(TIEMPO_10CM);
            frenar();
            
            MoveCursor(0,0);
            StringLCD("MIDIENDO TIERRA ");
            delay_ms(2000); 
            
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
            frenar();
            MoveCursor(0,0);
            StringLCD("MAPEO COMPLETO  ");
            MoveCursor(0,1);
            StringLCD("FIN DEL VIAJE   ");
            while(1); 
        }
    }
}