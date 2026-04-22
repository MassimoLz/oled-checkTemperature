#include <iostream>
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <algorithm>
#include <vector>
#include <array>
#include "font.h"
#include <cctype> // Necessario per toupper
#include <bitset>
#include <fstream> // Necessaria per leggere i file (ifstream)
#include <chrono>
#include <thread>

class OledDisplay
{
private:
    int riga = 0;
    int pagina = 0;
    int channel;
    int dcPin;
    int rstPin;
    std::vector<unsigned char> schermo;

public:
    OledDisplay(int spiChannel, int dc, int rst)
        : channel(spiChannel), dcPin(dc), rstPin(rst), schermo(1024, 0), pagina(0), riga(0) {}

    bool begin()
    {
        if (wiringPiSetupGpio() == -1)
            return false;
        if (wiringPiSPISetup(channel, 8000000) == -1)
            return false;

        pinMode(dcPin, OUTPUT);
        pinMode(rstPin, OUTPUT);

        reset();
        initOLED(); // Chiamiamo la sequenza di avvio vitale!
        return true;
    }

    void reset()
    {
        digitalWrite(rstPin, LOW);
        delay(50);
        digitalWrite(rstPin, HIGH);
        delay(50);
    }

    void sendCommand(unsigned char cmd)
    {
        digitalWrite(dcPin, LOW);
        wiringPiSPIDataRW(channel, &cmd, 1);
    }

    void initOLED()
    {
        sendCommand(0xAE); // Display OFF

        // --- COMANDI PER RUOTARE DI 180° ---
        sendCommand(0xA1); // Segment Remap: mappa la colonna 127 a SEG0 (Inverte Orizzontalmente)
        sendCommand(0xC8); // COM Scan Direction: scansiona da COM[N-1] a COM0 (Inverte Verticalmente)
        // ------------------------------------

        sendCommand(0x20);
        sendCommand(0x00); // Horizontal mode

        sendCommand(0x8D);
        sendCommand(0x14); // Charge Pump

        sendCommand(0xAF); // Display ON
    }

    void sendBuffer()
    {
        // 1. Diciamo al display di partire dalla colonna 0 e pagina 0
        sendCommand(0x21);
        sendCommand(0x00);
        sendCommand(0x7F); // 21= righi,00=riga zero,7f=riga 127
        sendCommand(0x22);
        sendCommand(0x00);
        sendCommand(0x07); // 22=pagine 00=pag 0 0=pag 8

        // 2. Alziamo il pin DC e mandiamo i 1024 byte
        digitalWrite(dcPin, HIGH);
        std::vector<unsigned char> sacrifico = schermo;
        // wiringPiSPIDataRW sovrascrive l'array, quindi passiamo i dati del vettore
        wiringPiSPIDataRW(channel, sacrifico.data(), sacrifico.size());
    }
    void clear()
    {
        for (int i = 0; i < schermo.size(); i++)
        {
            schermo[i] = 0;
        }
        sendBuffer();
        riga=0;
        pagina=0;
    }
    void disegnariga(unsigned char seguenza, int rigaa, int pagina)
    {
        unsigned int indice = pagina * 128 + rigaa;
        
        if (indice > 1023)
        {
            indice = indice % 1023;
        }
        schermo[indice] = seguenza;
        riga++;
    }
    void Scrivi(std::string parola)
    {
        std::array<unsigned char, 5> byte;
        char x = 'a';
        for (int i = 0; i < parola.size(); i++)
        {
            x = std::toupper(parola[i]);
            byte = font_mappa.at(x);
            Scale(byte,1);
        }
    }
    void scrivi(std::string parola, int scale)
    {
        std::array<unsigned char, 5> byte;
        char x = 'a';
        for (int i = 0; i < parola.size(); i++)
        {
            x = std::toupper(parola[i]);
            byte = font_mappa.at(x);
            Scale(byte, scale);
        }
    }

    void Scale(const std::array<unsigned char, 5> &font, int scale)
{
    //controllo per andare a capo
    if (riga + (6 * scale) > 127)
    {
        riga = 0;        // Torniamo all'inizio a sinistra
        pagina += scale; // Scendiamo in basso della grandezza del font
        
    }

    int riga_iniziale = riga;

    for (int i = 0; i < 5; i++)
    {
        unsigned int byte_sorgente = font[i];
        int contatore = 0;
        
        // Inizializziamo alla pagina base. Questa salirà man mano che scaliamo in verticale.
        int pagina_corrente = pagina; 
        
        std::array<unsigned char, 8> array_dispose;
        array_dispose.fill(0);

        for (int b = 0; b < 8; b++)
        {
            unsigned char valore_bit = (byte_sorgente & (1 << b)) ? 1 : 0;

            for (int j = 0; j < scale; j++)
            {
                array_dispose[contatore] = valore_bit;
                contatore++;

                if (contatore == 8)
                {
                    unsigned char byte_da_inviare = 0;

                    for (int k = 0; k < 8; k++)
                    {
                        byte_da_inviare |= (array_dispose[k] << k);
                    }

                    // 2. CORREZIONE DISEGNO: Usiamo SOLO pagina_corrente
                    for (int s_colonna = 0; s_colonna < scale; s_colonna++)
                    {
                        disegnariga(byte_da_inviare, riga_iniziale + s_colonna, pagina_corrente);
                    }

                    contatore = 0;
                    array_dispose.fill(0);
                    pagina_corrente++; // Passa alla parte bassa del carattere
                }
            }
        }
        riga_iniziale += scale; // Spostati a destra per la colonna successiva
    }

    // 3. CORREZIONE SPAZIO: Disegniamo lo spazio vuoto in modo pulito
    // Per ogni pagina verticale occupata dal carattere scalato...
    for (int p = 0; p < scale; p++)
    {
        // ...disegniamo una o più colonne di zeri per "staccare" le lettere
        for (int s = 0; s < scale; s++) 
        {
            disegnariga(0x00, riga_iniziale + s, pagina + p);
        }
    }
    
    // Aggiorniamo la X globale aggiungendo anche lo spazio vuoto appena creato
    riga = riga_iniziale + scale; 
}
};
float leggiTemperaturaCPU() {
    float temperatura = 0.0f;
    
    // Apriamo il file di sistema in modalità lettura
    std::ifstream file_temperatura("/sys/class/thermal/thermal_zone0/temp");

    // Controlliamo se il file si è aperto correttamente
    if (file_temperatura.is_open()) {
        int milligradi;
        file_temperatura >> milligradi; // Leggiamo il numero dal file
        
        // Convertiamo i milligradi in gradi Celsius
        temperatura = milligradi / 1000.0f; 
        
        file_temperatura.close(); // Chiudiamo il file
    } else {
        std::cerr << "Errore: Impossibile leggere il file della temperatura." << std::endl;
        return -99.0f; // Restituisce un valore assurdo in caso di errore
    }

    return temperatura;
}
int main()
{
    OledDisplay oled(0, 24, 25);
    int temp =leggiTemperaturaCPU();
    std::string temperatura = std::to_string(temp);
    if (!oled.begin())
    {
        std::cerr << "Errore inizializzazione hardware!" << std::endl;
        return 1;
    }
    while (true)
    {
       oled.scrivi("temperatura:" + temperatura +" C.",2);
    oled.sendBuffer(); 
    std::this_thread::sleep_for(std::chrono::seconds(2));
    oled.clear();
    }
    
    std::cout << "Display OLED pronto e buffer inviato!" << std::endl;
    return 0;
}
// Funzione che legge la temperatura e la restituisce come numero con la virgola (float)
