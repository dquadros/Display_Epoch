//
// Epoch - contador de segundos a partir da fundação do GHC
//

#include <16f882.h>

#device adc=8
#use delay(clock=8000000)
#fuses NOWDT, NOCPD, NOPROTECT
#fuses MCLR, INTRC_IO

#use fast_io(A)
#use fast_io(B)
#use fast_io(C)

// Pinos utilizados
#use rs232 (baud=9600, UART1)

#define  DS1302_CE   PIN_B0
#define  DS1302_IO   PIN_B1
#define  DS1302_SCLK PIN_B2

#define	 DISP_S0	PIN_C0
#define	 DISP_S1	PIN_C1
#define	 DISP_S2	PIN_C2

#define	LED		PIN_C3

#define TAXA_DISP 3     // taxa de varredura do display
#define INTS_SEG  3906  // taxa de varredura do display

// Alguns registradores do DS1302
#define  DS1302_WP   	0x0E
#define  DS1302_TC   	0x10
#define  DS1302_CBURST	0x3E

// Dias por mês      31 28 31  30  31  30  31  31  30  31  30
const int dias[] = 	{ 0,31,59,90,120,151,181,212,243,273,304,334 };

// Caracteres ASCII
#define	BS	8
#define	LF	10
#define	CR	13
#define	ESC	27

const char hexa[] = "0123456789ABCDEF";

// EPOCH0 - fundação do GHC em segundos após 1/1/2000 00:00:00
#define	EPOCH0	351549263L

// valor a apresentar no display
int8 valor[16];

// valor a programar no relogio (ddmmaahhmmss)
int8 horario[12];

// data/hora para o relogio
byte relogio[8];

// Rotinas locais
void InitHw (void);
void LoadClk (void);
void TrataUart (void);

void DS1302_ReadClock (byte *regs);
void DS1302_WriteClock (byte *regs);
byte DS1302_Read (byte ender);
void DS1302_Write (byte ender, byte dado);
void DS1302_TxByte (byte b);
byte DS1302_RxByte ();

// Ponto de entrada e loop principal
void main (void)
{
	InitHw ();
	LoadClk ();
	enable_interrupts(GLOBAL);
	for (;;)
	{
		TrataUart();
	}
}

// Iniciação do hardware
void InitHw (void)
{
	// Bota para correr
	setup_oscillator (OSC_8MHZ);

	// Dispositivos não usados
    setup_comparator(NC_NC);
    setup_vref(FALSE);
    setup_adc(ADC_OFF);

	// E/S
	set_tris_a (0x00);	// RA0-RA7 output
	set_tris_b (0xFA);	// RB0 e RB2 output
	set_tris_c (0xF0);	// RC0-RC3 output
	output_low (LED);

	// Timer
    setup_timer_0(RTCC_INTERNAL|RTCC_DIV_2);
    setup_timer_1(T1_DISABLED);
	enable_interrupts (INT_RTCC);

	// Uart
	while (kbhit())
		getc();
}

// Coloca no display o valor do relógio não volátil
void LoadClk (void)
{
	byte i, aux;
	unsigned int32 time;

	DS1302_ReadClock (relogio);
	if ((relogio[3] & 0x3F) == 0)
	{
		// não programado
	    for (i = 0; i < 16; i++)
	    {
	        valor[i] = 0;
	    }
		return;
	}

    // Zera os bits não implementados no timer
    relogio[0] &= 0x7F;
    relogio[1] &= 0x7F;
    relogio[2] &= 0x3F;
    relogio[3] &= 0x3F;
    relogio[4] &= 0x1F;

    // Converte valores BCD para binário
    for (i = 0; i < 7; i++)
    {
        relogio[i] = (relogio[i] >> 4) * 10 + (relogio[i] & 0x0F);
    }

    // Determina o número de dias nos anos inteiros
    // Na faixa 2000 a 2099 os anos divisíveis por 4 são bissextos
    // Cada grupo de 4 anos tem 3*365+366 dias
    time = ((unsigned int32) (relogio[6] >> 2)) * (3L*365L+366L);
    aux = (relogio[6] & 3);
    if (aux > 0)
    {
        time += 366;
        if (aux > 1)
            time += 365;
        if (aux > 2)
            time += 365;
    }

    // Soma os dias até a data atual
    time += dias[relogio[4]-1];
    if ((aux == 0) && (relogio[4] > 2))
        time++;      // ano bissexto
    time += relogio[3]-1;

    // Soma as horas
    time = time*24UL + (unsigned int32) relogio[2];

    // Soma os minutos
    time = time*60UL + (unsigned int32) relogio[1];

    // Soma os segundos
    time = time*60UL + (unsigned int32) relogio[0];

	// Contar a partir do EPOCH0
	time -= EPOCH0;

	// coloca no valor do display
	disable_interrupts (INT_RTCC);
	for (i = 0; i < 16; i++)
	{
		valor[15-i] = time % 10L;
		time = time / 10L;
	}
	enable_interrupts (INT_RTCC);
}

// Trata comunicação com o PC
void TrataUart (void)
{
	char c;
	int i;

	if (kbhit())
	{
		c = getc();
		if (c != CR)
			return;

		output_high (LED);
		putchar (CR);
		putchar (LF);
		putchar ('>');
		i = c = 0;
		//ddmmaahhmmss
		while ((c != CR) || (i != 12))
		{
			c = getc();
			if ((c == ESC) || (c == 0))
				break;
			if ((c == BS) && (i > 0))
			{
				putchar(BS);
				putchar(' ');
				putchar(BS);
				i--;
			}
			else if ((i < 12) && (c >= '0') && (c <= '9'))
			{
				putchar(c);
				horario[i++] = c-'0';
			}
		}
		putchar (CR);
		putchar (LF);
		if (c != ESC)
		{
			relogio[0] = (horario[10] << 4) | horario[11];
			relogio[1] = (horario[8] << 4) | horario[9];
			relogio[2] = (horario[6] << 4) | horario[7];
			relogio[3] = (horario[0] << 4) | horario[1];
			relogio[4] = (horario[2] << 4) | horario[3];
			relogio[5] = 1;
			relogio[6] = (horario[4] << 4) | horario[5];
			relogio[7] = 0x80;

			for (i = 0; i < 8; i++)
			{
				putchar (hexa[relogio[i] >> 4]);
				putchar (hexa[relogio[i] & 0x0F]);
				putchar (' ');
			}
			putchar (CR);
			putchar (LF);

			DS1302_WriteClock (relogio);
			LoadClk ();
		}
		output_low (LED);
	}
}

//////////////////////////////////////////////
// Interrupcao de tempo real
// Ocorre a cada 512 uSeg
#int_RTCC
void RTCC_isr()
{
	static unsigned int16 cont_seg = INTS_SEG;
    static unsigned int8 cont_disp = TAXA_DISP;
    static unsigned int8 disp = 0;

    // Atualização da contagem de tempo
	if (--cont_seg == 0)
	{
		int8 i=15;
		while ((i > 0) && (++valor[i] == 10))
			valor[i--] = 0;
		cont_seg = INTS_SEG;
	}

    // Atualização do Display
    if (--cont_disp == 0)
    {
	    // seleciona o display atual
		if (disp & 1)
			output_high (DISP_S0);
		else
			output_low (DISP_S0);
		if (disp & 2)
			output_high (DISP_S1);
		else
			output_low (DISP_S1);
		if (disp & 4)
			output_high (DISP_S2);
		else
			output_low (DISP_S2);
	
	    // coloca os valores
		output_a ((valor[15-disp] << 4) | valor[disp]);
	      
	    // passa para o dígito seguinte
	    disp = (disp + 1) & 7;
      	cont_disp = TAXA_DISP;
    }
}

//////////////////////////////////////////////
// DS1302 - Relógio

// Lê relogio em modo burst
void DS1302_ReadClock (byte *regs)
{
   byte i;

   output_high (DS1302_CE);
   delay_us (4);
   DS1302_TxByte (DS1302_CBURST | 0x81);
   for (i = 0; i < 8; i++)
   {
      *regs++ = DS1302_RxByte ();
      delay_us (4);
   }
   output_low (DS1302_CE);
}

// Escreve relogio em modo burst
void DS1302_WriteClock (byte *regs)
{
   byte i;

   DS1302_Write (DS1302_WP, 0x00);
   DS1302_Write (DS1302_TC, 0xA9);	// tricle charger, 2 diodes, 2Kohms

   output_high (DS1302_CE);
   delay_us (4);
   DS1302_TxByte (DS1302_CBURST | 0x80);
   for (i = 0; i < 8; i++)
   {
      DS1302_TxByte (*regs++);
      delay_us (4);
   }
   output_low (DS1302_CE);
}

// Lê um byte de um endereço do DS1302
byte DS1302_Read (byte ender)
{
   byte result;
   
   output_high (DS1302_CE);
   delay_us (2);
   DS1302_TxByte (ender | 0x81);
   result = DS1302_RxByte ();
   delay_us (2);
   output_low (DS1302_CE);
   return result;
}

// Escreve um byte em um endereço do DS1302
void DS1302_Write (byte ender, byte dado)
{
   output_high (DS1302_CE);
   delay_us (2);
   DS1302_TxByte (ender | 0x80);
   DS1302_TxByte (dado);
   delay_us (2);
   output_low (DS1302_CE);
}

// Envia um byte ao DS1302
void DS1302_TxByte (byte b)
{
   int i;
   
   set_tris_b (0xF8);	// I/O é output
   for (i = 0; i < 8; i++)
   {
      if (b & 1)
         output_high (DS1302_IO);
      else
         output_low (DS1302_IO);
      delay_us (1);
      output_high (DS1302_SCLK);
      delay_us (1);
      output_low (DS1302_SCLK);
      b = b >> 1;
   }
   set_tris_b (0xFA);	// I/O volta a ser input
}

// Recebe um byte do DS1302
byte DS1302_RxByte ()
{
   int i;
   byte dado;
   
   for (i = 0; i < 8; i++)
   {
      delay_us (1);
      dado = dado >> 1;
      if (input (DS1302_IO))
         dado |= 0x80;
      output_high (DS1302_SCLK);
      delay_us (1);
      output_low (DS1302_SCLK);
   }
   return dado;
}

