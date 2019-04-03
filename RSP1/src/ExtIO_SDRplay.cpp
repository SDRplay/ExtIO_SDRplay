
/* *******************************************************************************/
/*					EXTIO Plugin Interface for SDRplay RSP1						 */
/*									V4.1										 */
/*********************************************************************************/

#include <WinSock2.h>
#include <Windows.h>
#include <WindowsX.h>
#include <commctrl.h>
#include <process.h>
#include <tchar.h>
#include <new>
#include <stdio.h>
#include <math.h>
#include <dos.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include "resource.h"
#include "ExtIO_SDRplay.h"
#include "mir_sdr.h"
#include <sys/types.h>
#include <sys/timeb.h>
#include <ctime>
#include "Shlwapi.h"
#include <stdlib.h>
#include <ShlObj.h>
#include <sys/stat.h>

using namespace std;

// main debug control - check output with debugview
//#define DEBUG_ENABLE

// hardware debug output (watch with debugview)
#define RSP_DEBUG 0
// station lookup debug - output to station control panel
//#define DEBUG_STATION

#define NOT_USED(p) ((void)(p))

#define MAXNUMOFDEVS 4
mir_sdr_DeviceT devices[4];
unsigned int numDevs;

#define EXTIO_HWTYPE_16B 3
#define LO120MHz 24576000
#define LO144MHz 22000000
#define LO168MHz 19200000
#define ID_AGCUPDATE 202
#define IDC_BUFFER 200
#define STATIC 0
#define PERIODIC1 1
#define PERIODIC2 2
#define PERIODIC3 3
#define ONESHOT 4
#define CONTINUOUS 5
#define DCTrackTimeInterval 2.93
#define BUFFER_LEN (4096)
#define USE_GR_ALT_MODE       mir_sdr_SetGrModeT (2)
#define DONT_USE_GR_ALT_MODE   (0)
static bool profileChanged = false;
static bool stationLookup = false;
static bool localDBExists = false;
static bool workOffline = false;
int TACIndex = 0;
int ITUIndex = 0;

typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);

//Globals for monioring SDRplay Condition.
static int DcCompensationMode;
static int TrackingPeriod;
static int RefreshPeriodMemory;
static bool PostTunerDcCompensation;
double TrackTime;
static double Frequency = 98.8;			// nominal / desired frequency
int LowerFqLimit;
int IFmodeIdx = 0;
float FqOffsetPPM = 0;
volatile int GainReduction = 40;		// variables accessed from different threads should be volatile!
volatile int LNAGainReduction = 0;
int SystemGainReduction;
int zero = 0;
int *pZERO = &zero;                 // used to pass int pointer to Reinit where the value isn't used or the return value isn't significant
mir_sdr_If_kHzT IFMode = mir_sdr_IF_Zero;
mir_sdr_Bw_MHzT Bandwidth = mir_sdr_BW_1_536;
mir_sdr_LoModeT LOMode = mir_sdr_LO_Auto;

volatile int BandwidthIdx = 3;

int DecimationIdx = 0;

int AGCsetpoint = -30;
mir_sdr_AgcControlT AGCEnabled = mir_sdr_AGC_5HZ;
int down_conversion_enabled = 1;
int LOplan = LO120MHz;
bool LOplanAuto = true;
bool LOChangeNeeded = false;
TCHAR msgbuf[255];
static int LastGrToggle = -1;
clock_t StartTime, CurrentTime;
int ExecutionTime;
static double lastFreq = -1.0;
static int lastFreqBand = -1;
static int lastGainReduction = -1;
bool invertSpectrum = false;
double FreqOffsetInvSpec = 0.0;
int SampleCount = 0;				// number of I/Q frames
static bool loadedProfiles = false;
static bool definedHotkeys = false;
static bool LNAEnable = true;
static unsigned int LnaGr = 0;
static char stationTTipText[8192];
static bool displayStationTip = false;

TCHAR apiVersion[32];
TCHAR apiPath[255];
WCHAR regPath[255];

//Other Globals
static int buffer_len_samples;		// number of samples in one buffer (= 2 * number of I/Q frames)
HWND ghwndDlg = 0;
volatile int ThreadExitFlag = 0;
bool Initiated = false;
bool Running = false;
static int ppm_default=0;
int SampleRateIdx = 0;
char demodMode;
int DecimateEnable = 0;
int DecimateFactor = 0;

short CallbackBuffer[];

struct DEC
{
	int DecIndex;
	int DecValue;
};

struct DEC Decimation[] =
{
	{ 0,  0 },
	{ 1,  2 },
	{ 2,  4 },
	{ 3,  8 },
	{ 4, 16 },
	{ 5, 32 },
	{ 6, 64 }
};

typedef struct sr {
	double value;
	TCHAR *name;
} sr_t;

static sr_t samplerates[] = {
	{ 2.0,  TEXT("2.0 MHz") },
	{ 3.0,  TEXT("3.0 MHz") },
	{ 4.0,  TEXT("4.0 MHz") },
	{ 5.0,  TEXT("5.0 MHz") },
	{ 6.0,  TEXT("6.0 MHz") },
	{ 7.0,  TEXT("7.0 MHz") },
	{ 8.0,  TEXT("8.0 MHz") },
	{ 9.0,  TEXT("9.0 MHz") },
	{10.0, TEXT("10.0 MHz") }
};

struct bw_t {
	mir_sdr_Bw_MHzT bwType;
	double			BW;
};

static bw_t bandwidths[] = {
	{ mir_sdr_BW_0_200, 0.2   },
	{ mir_sdr_BW_0_300, 0.3   },
	{ mir_sdr_BW_0_600, 0.6   },
	{ mir_sdr_BW_1_536, 1.536 },
	{ mir_sdr_BW_5_000, 5.000 },
	{ mir_sdr_BW_6_000, 6.000 },
	{ mir_sdr_BW_7_000, 7.000 },
	{ mir_sdr_BW_8_000, 8.000 }
};

TCHAR p1fname[MAX_PATH] = _T("");
TCHAR p2fname[MAX_PATH] = _T("");
TCHAR p3fname[MAX_PATH] = _T("");
TCHAR p4fname[MAX_PATH] = _T("");
TCHAR p5fname[MAX_PATH] = _T("");
TCHAR p6fname[MAX_PATH] = _T("");
TCHAR p7fname[MAX_PATH] = _T("");
TCHAR p8fname[MAX_PATH] = _T("");

typedef struct {
	char *targetAreaCode;
	char *description;
} tac;

typedef struct {
	char *ituCode;
	char *countryName;
} itu;

typedef struct {
	char *langCode;
	char *description;
	char *silcode;
} lang;

struct station {
	char *freq;
	char *time;
	char *days;
	char *itu;
	char *name;
	char *language;
	char *tac;
	char minTime[5];
	char maxTime[5];
	struct station *next;
};

static itu ituCodes[] = {
	{ "ZZZ", "Do not do any country matching" },
	{ "ABW", "Aruba" },
	{ "AFG", "Afghanistan" },
	{ "AFS", "South Africa" },
	{ "AGL", "Angola" },
	{ "AIA", "Anguilla" },
	{ "ALB", "Albania" },
	{ "ALG", "Algeria" },
	{ "ALS", "Alaska" },
	{ "AMS", "Saint Paul & Amsterdam Is." },
	{ "AND", "Andorra" },
	{ "AOE", "Western Sahara" },
	{ "ARG", "Argentina" },
	{ "ARM", "Armenia" },
	{ "ARS", "Saudi Arabia" },
	{ "ASC", "Ascension Island" },
	{ "ATA", "Antarctica" },
	{ "ATG", "Antigua and Barbuda" },
	{ "ATN", "Netherlands Leeward Antilles(dissolved in 2010)" },
	{ "AUS", "Australia" },
	{ "AUT", "Austria" },
	{ "AZE", "Azerbaijan" },
	{ "AZR", "Azores" },
	{ "B", "Brasil" },
	{ "BAH", "Bahamas" },
	{ "BDI", "Burundi" },
	{ "BEL", "Belgium" },
	{ "BEN", "Benin" },
	{ "BER", "Bermuda" },
	{ "BES", "Bonaire, St Eustatius, Saba(Dutch islands in the Caribbean)" },
	{ "BFA", "Burkina Faso" },
	{ "BGD", "Bangla Desh" },
	{ "BHR", "Bahrain" },
	{ "BIH", "Bosnia - Herzegovina" },
	{ "BIO", "Chagos Is. (Diego Garcia) (British Indian Ocean Territory)" },
	{ "BLM", "Saint - Barthelemy" },
	{ "BLR", "Belarus" },
	{ "BLZ", "Belize" },
	{ "BOL", "Bolivia" },
	{ "BOT", "Botswana" },
	{ "BRB", "Barbados" },
	{ "BRU", "Brunei Darussalam" },
	{ "BTN", "Bhutan" },
	{ "BUL", "Bulgaria" },
	{ "BVT", "Bouvet" },
	{ "CAB", "Cabinda *" },
	{ "CAF", "Central African Republic" },
	{ "CAN", "Canada" },
	{ "CBG", "Cambodia" },
	{ "CEU", "Ceuta *" },
	{ "CG7", "Guantanamo Bay" },
	{ "CHL", "Chile" },
	{ "CHN", "China(People's Republic)" },
	{ "CHR", "Christmas Island(Indian Ocean)" },
	{ "CKH", "Cook Island" },
	{ "CLA", "Clandestine stations *" },
	{ "CLM", "Colombia" },
	{ "CLN", "Sri Lanka" },
	{ "CME", "Cameroon" },
	{ "CNR", "Canary Islands" },
	{ "COD", "Democratic Republic of Congo(capital Kinshasa)" },
	{ "COG", "Republic of Congo(capital Brazzaville)" },
	{ "COM", "Comores" },
	{ "CPT", "Clipperton" },
	{ "CPV", "Cape Verde Islands" },
	{ "CRO", "Crozet Archipelago" },
	{ "CTI", "Ivory Coast(Côte d'Ivoire)" },
	{ "CTR", "Costa Rica" },
	{ "CUB", "Cuba" },
	{ "CUW", "Curacao" },
	{ "CVA", "Vatican State" },
	{ "CYM", "Cayman Islands" },
	{ "CYP", "Cyprus" },
	{ "CZE", "Czech Republic" },
	{ "D", "Germany" },
	{ "DJI", "Djibouti" },
	{ "DMA", "Dominica" },
	{ "DNK", "Denmark" },
	{ "DOM", "Dominican Republic" },
	{ "E", "Spain" },
	{ "EGY", "Egypt" },
	{ "EQA", "Ecuador" },
	{ "ERI", "Eritrea" },
	{ "EST", "Estonia" },
	{ "ETH", "Ethiopia" },
	{ "EUR", "Iles Europe & Bassas da India *" },
	{ "F", "France" },
	{ "FIN", "Finland" },
	{ "FJI", "Fiji" },
	{ "FLK", "Falkland Islands" },
	{ "FRO", "Faroe Islands" },
	{ "FSM", "Federated States of Micronesia" },
	{ "G", "United Kingdom of Great Britain and Northern Ireland" },
	{ "GAB", "Gabon" },
	{ "GEO", "Georgia" },
	{ "GHA", "Ghana" },
	{ "GIB", "Gibraltar" },
	{ "GLP", "Guadeloupe" },
	{ "GMB", "Gambia" },
	{ "GNB", "Guinea - Bissau" },
	{ "GNE", "Equatorial Guinea" },
	{ "GPG", "Galapagos *" },
	{ "GRC", "Greece" },
	{ "GRD", "Grenada" },
	{ "GRL", "Greenland" },
	{ "GTM", "Guatemala" },
	{ "GUF", "French Guyana" },
	{ "GUI", "Guinea" },
	{ "GUM", "Guam / Guahan" },
	{ "GUY", "Guyana" },
	{ "HKG", "Hong Kong, part of China" },
	{ "HMD", "Heard & McDonald Islands" },
	{ "HND", "Honduras" },
	{ "HNG", "Hungary" },
	{ "HOL", "The Netherlands" },
	{ "HRV", "Croatia" },
	{ "HTI", "Haiti" },
	{ "HWA", "Hawaii" },
	{ "HWL", "Howland & Baker" },
	{ "I", "Italy" },
	{ "ICO", "Cocos(Keeling) Island" },
	{ "IND", "India" },
	{ "INS", "Indonesia" },
	{ "IRL", "Ireland" },
	{ "IRN", "Iran" },
	{ "IRQ", "Iraq" },
	{ "ISL", "Iceland" },
	{ "ISR", "Israel" },
	{ "IW", "International Waters" },
	{ "IWA", "Ogasawara(Bonin, Iwo Jima) *" },
	{ "J", "Japan" },
	{ "JAR", "Jarvis Island" },
	{ "JDN", "Juan de Nova *" },
	{ "JMC", "Jamaica" },
	{ "JMY", "Jan Mayen *" },
	{ "JON", "Johnston Island" },
	{ "JOR", "Jordan" },
	{ "JUF", "Juan Fernandez Island *" },
	{ "KAL", "Kaliningrad *" },
	{ "KAZ", "Kazakstan / Kazakhstan" },
	{ "KEN", "Kenya" },
	{ "KER", "Kerguelen" },
	{ "KGZ", "Kyrgyzstan" },
	{ "KIR", "Kiribati" },
	{ "KNA", "St Kitts and Nevis" },
	{ "KOR", "Korea, South(Republic)" },
	{ "KRE", "Korea, North(Democratic People's Republic)" },
	{ "KWT", "Kuwait" },
	{ "LAO", "Laos" },
	{ "LBN", "Lebanon" },
	{ "LBR", "Liberia" },
	{ "LBY", "Libya" },
	{ "LCA", "Saint Lucia" },
	{ "LIE", "Liechtenstein" },
	{ "LSO", "Lesotho" },
	{ "LTU", "Lithuania" },
	{ "LUX", "Luxembourg" },
	{ "LVA", "Latvia" },
	{ "MAC", "Macao" },
	{ "MAF", "St Martin" },
	{ "MAU", "Mauritius" },
	{ "MCO", "Monaco" },
	{ "MDA", "Moldova" },
	{ "MDG", "Madagascar" },
	{ "MDR", "Madeira" },
	{ "MDW", "Midway Islands" },
	{ "MEL", "Melilla *" },
	{ "MEX", "Mexico" },
	{ "MHL", "Marshall Islands" },
	{ "MKD", "Macedonia(F.Y.R.)" },
	{ "MLA", "Malaysia" },
	{ "MLD", "Maldives" },
	{ "MLI", "Mali" },
	{ "MLT", "Malta" },
	{ "MNE", "Montenegro" },
	{ "MNG", "Mongolia" },
	{ "MOZ", "Mozambique" },
	{ "MRA", "Northern Mariana Islands" },
	{ "MRC", "Morocco" },
	{ "MRN", "Marion & Prince Edward Islands" },
	{ "MRT", "Martinique" },
	{ "MSR", "Montserrat" },
	{ "MTN", "Mauritania" },
	{ "MWI", "Malawi" },
	{ "MYA", "Myanmar(Burma) (also BRM)" },
	{ "MYT", "Mayotte" },
	{ "NCG", "Nicaragua" },
	{ "NCL", "New Caledonia" },
	{ "NFK", "Norfolk Island" },
	{ "NGR", "Niger" },
	{ "NIG", "Nigeria" },
	{ "NIU", "Niue" },
	{ "NMB", "Namibia" },
	{ "NOR", "Norway" },
	{ "NPL", "Nepal" },
	{ "NRU", "Nauru" },
	{ "NZL", "New Zealand" },
	{ "OCE", "French Polynesia" },
	{ "OMA", "Oman" },
	{ "PAK", "Pakistan" },
	{ "PAQ", "Easter Island" },
	{ "PHL", "Philippines" },
	{ "PHX", "Phoenix Is." },
	{ "PLM", "Palmyra Island" },
	{ "PLW", "Palau" },
	{ "PNG", "Papua New Guinea" },
	{ "PNR", "Panama" },
	{ "POL", "Poland" },
	{ "POR", "Portugal" },
	{ "PRG", "Paraguay" },
	{ "PRU", "Peru" },
	{ "PRV", "Okino - Tori - Shima(Parece Vela) *" },
	{ "PSE", "Palestine" },
	{ "PTC", "Pitcairn" },
	{ "PTR", "Puerto Rico" },
	{ "QAT", "Qatar" },
	{ "REU", "La Réunion" },
	{ "ROD", "Rodrigues" },
	{ "ROU", "Romania" },
	{ "RRW", "Rwanda" },
	{ "RUS", "Russian Federation" },
	{ "S", "Sweden" },
	{ "SAP", "San Andres & Providencia *" },
	{ "SDN", "Sudan" },
	{ "SEN", "Senegal" },
	{ "SEY", "Seychelles" },
	{ "SGA", "South Georgia Islands *" },
	{ "SHN", "Saint Helena" },
	{ "SLM", "Solomon Islands" },
	{ "SLV", "El Salvador" },
	{ "SMA", "Samoa(American)" },
	{ "SMO", "Samoa" },
	{ "SMR", "San Marino" },
	{ "SNG", "Singapore" },
	{ "SOK", "South Orkney Islands *" },
	{ "SOM", "Somalia" },
	{ "SPM", "Saint Pierre et Miquelon" },
	{ "SRB", "Serbia" },
	{ "SRL", "Sierra Leone" },
	{ "SSD", "South Sudan" },
	{ "SSI", "South Sandwich Islands *" },
	{ "STP", "Sao Tome & Principe" },
	{ "SUI", "Switzerland" },
	{ "SUR", "Suriname" },
	{ "SVB", "Svalbard *" },
	{ "SVK", "Slovakia" },
	{ "SVN", "Slovenia" },
	{ "SWZ", "Swaziland" },
	{ "SXM", "Sint Maarten" },
	{ "SYR", "Syria" },
	{ "TCA", "Turks and Caicos Islands" },
	{ "TCD", "Tchad" },
	{ "TGO", "Togo" },
	{ "THA", "Thailand" },
	{ "TJK", "Tajikistan" },
	{ "TKL", "Tokelau" },
	{ "TKM", "Turkmenistan" },
	{ "TLS", "Timor - Leste" },
	{ "TON", "Tonga" },
	{ "TRC", "Tristan da Cunha" },
	{ "TRD", "Trinidad and Tobago" },
	{ "TUN", "Tunisia" },
	{ "TUR", "Turkey" },
	{ "TUV", "Tuvalu" },
	{ "TWN", "Taiwan *" },
	{ "TZA", "Tanzania" },
	{ "UAE", "United Arab Emirates" },
	{ "UGA", "Uganda" },
	{ "UKR", "Ukraine" },
	{ "UN", "United Nations *" },
	{ "URG", "Uruguay" },
	{ "USA", "United States of America" },
	{ "UZB", "Uzbekistan" },
	{ "VCT", "Saint Vincent and the Grenadines" },
	{ "VEN", "Venezuela" },
	{ "VIR", "American Virgin Islands" },
	{ "VRG", "British Virgin Islands" },
	{ "VTN", "Vietnam" },
	{ "VUT", "Vanuatu" },
	{ "WAK", "Wake Island" },
	{ "WAL", "Wallis and Futuna" },
	{ "XBY", "Abyei area" },
	{ "XGZ", "Gaza strip" },
	{ "XSP", "Spratly Islands" },
	{ "XUU", "Unidentified" },
	{ "XWB", "West Bank" },
	{ "YEM", "Yemen" },
	{ "ZMB", "Zambia" },
	{ "ZWE", "Zimbabwe" }
};

static tac targetAreaCodes[] = {
	{ "ZZZ", "Do not do any target area matching" },
	{ "Af", "Africa" },
	{ "Am", "America(s)" },
	{ "As", "Asia" },
//	{ "C", "Central" },
	{ "Car", "Caribbean, Gulf of Mexico, Florida Waters" },
	{ "Cau", "Caucasus" },
	{ "CIS", "Commonwealth of Independent States (Former Soviet Union)" },
	{ "CNA", "Central North America" },
//	{ "E", "East" },
	{ "ENA", "Eastern North America" },
	{ "Eu", "Europe (often including North Africa/Middle East)" },
	{ "FE", "Far East" },
	{ "LAm", "Latin America (Central and South America)" },
	{ "ME", "Middle East" },
//	{ "N", "North" },
	{ "NAO", "North Atlantic Ocean" },
	{ "Oc", "Oceania (Australia, New Zealand, Pacific Ocean)" },
//	{ "S", "South" },
	{ "SAO", "South Atlantic Ocean" },
	{ "SEA", "South East Asia" },
	{ "SEE", "South East Europe" },
	{ "Sib", "Siberia" },
	{ "Tib", "Tibet" },
//	{ "W", "West" },
	{ "WIO", "Western Indian Ocean" },
	{ "WNA", "Western North America" }
};

static lang languageCodes[] = {
	{ "-CW", "Morse Station", "[]" },
	{ "-MX", "Music", "[]" },
	{ "-TS", "Time Signal Station", "[]" },
	{ "A", "Arabic(300m)", "[arb]" },
	{ "AB", "Abkhaz : Georgia - Abkhazia(0.1m)", "[abk]" },
	{ "AC", "Aceh : Indonesia - Sumatera(3m)", "[ace]" },
	{ "ACH", "Achang / Ngac'ang: Myanmar, South China (60,000)", "[acn]" },
	{ "AD", "Adygea / Adyghe / Circassian : Russia - Caucasus(0.5m)", "[ady]" },
	{ "ADI", "Adi : India - Assam,Arunachal Pr. (0.1m)", "[adi]" },
	{ "AF", "Afrikaans : South Africa, Namibia(5m)", "[afr]" },
	{ "AFA", "Afar : Djibouti(0.3m), Ethiopia(0.45m), Eritrea(0.3m)", "[aar]" },
	{ "AFG", "Pashto and Dari(main Afghan languages, see there)", "[]" },
	{ "AH", "Amharic : Ethiopia(22m)", "[amh]" },
	{ "AJ", "Adja / Aja - Gbe : Benin, Togo(0.5m)", "[ajg]" },
	{ "AK", "Akha : Burma(0.2m), China - Yunnan(0.13m)", "[ahk]" },
	{ "AL", "Albanian : Albania(Tosk)(3m), Macedonia / Yugoslavia(Gheg)(2m)", "[sqi]" },
	{ "ALG", "Algerian(Arabic) : Algeria(28m)", "[arq]" },
	{ "AM", "Amoy : S China(25m), Taiwan(15m), SoEaAsia(5m); dialect of Minnan", "[nan]" },
	{ "AMD", "Tibetan Amdo(Tibet, Qinghai, Gansu, Sichuan: 2m)", "[adx]" },
	{ "Ang", "Angelus programme of Vaticane Radio", "[]" },
	{ "AR", "Armenian : Armenia(3m), USA(1m), RUS,GEO,SYR,LBN,IRN,EGY", "[hye]" },
	{ "ARO", "Aromanian / Vlach : Greece, Albania, Macedonia(0.1m)", "[rup]" },
	{ "ARU", "Languages of Arunachal, India(collectively)", "[]" },
	{ "ASS", "Assamese : India - Assam(13m)", "[asm]" },
	{ "ASY", "Assyrian / Syriac / Neo - Aramaic : Iraq, Iran, Syria(0.2m)", "[aii]" },
	{ "ATS", "Atsi / Zaiwa : Myanmar(13,000), China - Yunnan(70,000)", "[atb]" },
	{ "Aud", "Papal Audience(Vaticane Radio)", "[]" },
	{ "AV", "Avar : Dagestan, S Russia(0.7m)", "[ava]" },
	{ "AW", "Awadhi : N&Ce India(3m)", "[awa]" },
	{ "AY", "Aymara : Bolivia(2m)", "[ayr]" },
	{ "AZ", "Azeri / Azerbaijani : Azerbaijan(6m)", "[azj]" },
	{ "BAD", "Badaga : India - Tamil Nadu(0.13m)", "[bfq]" },
	{ "BAG", "Bagri : India - Punjab(0.6m), Pakistan(0.2m)", "[bgq]" },
	{ "BAI", "Bai : China - Yunnan(1.2m)", "[bca]" },
	{ "BAJ", "Bajau : Malaysia - Sabah(50,000)", "[bdr]" },
	{ "BAL", "Balinese : Indonesia - Bali(3m)", "[ban]" },
	{ "BAN", "Banjar / Banjarese : Indonesia - Kalimantan(3.5m)", "[bjn]" },
	{ "BAO", "Baoulé : Cote d'Ivoire (2m)", "[bci]" },
	{ "BAR", "Bari : South Sudan(0.4m)", "[bfa]" },
	{ "BAS", "Bashkir / Bashkort : Russia - Bashkortostan(1m)", "[bak]" },
	{ "BAY", "Bayash / Boyash(gypsy dialect of Romanian) : Serbia, Croatia(10,000)", "[]" },
	{ "BB", "Braj Bhasa / Braj Bhasha / Brij : India - Rajasthan(0.6m)", "[bra]" },
	{ "BC", "Baluchi : Pakistan(5m)", "[bal]" },
	{ "BE", "Bengali / Bangla : Bangladesh(110m), India(82m)", "[ben]" },
	{ "BED", "bedawiyet / Bedawi / Beja : Sudan(1m)", "[bej]" },
	{ "BEM", "Bemba : Zambia(3m)", "[bem]" },
	{ "BGL", "Bagheli : N India(3m)", "[bfy]" },
	{ "BH", "Bhili : India - Madhya Pradesh, Gujarat(3.3m)", "[bhb]" },
	{ "BHN", "Bahnar : Vietnam(160,000)", "[bdq]" },
	{ "BHT", "Bhatri : India - Chhattisgarh,Maharashtra(0.2m)", "[bgw]" },
	{ "BI", "Bilen / Bile : Eritrea - Keren(90,000)", "[byn]" },
	{ "BID", "Bidayuh languages : Malaysia - Sarawak(70,000)", "[sdo]" },
	{ "BIS", "Bisaya : Malaysia - Sarawak,Sabah(20,000), Brunei(40,000)", "[bsb]" },
	{ "BJ", "Bhojpuri / Bihari : India(38m), Nepal(1.7m)", "[bho]" },
	{ "BK", "Balkarian : Russia - Caucasus(0.3m)", "[krc]" },
	{ "BLK", "Balkan Romani : Bulgaria(0.3m), Serbia(0.1m), Macedonia(0.1m)", "[rmn]" },
	{ "BLT", "Balti : NE Pakistan(0.3m)", "[bft]" },
	{ "BM", "Bambara / Bamanankan : Mali(4m)", "[bam]" },
	{ "BNA", "Borana Oromo / Afan Oromo : Ethiopia(4m)", "[gax]" },
	{ "BNG", "Bangala / Mbangala : Central Angola(0.4m)", "[mxg]" },
	{ "BNI", "Baniua / Baniwa : Brazil - Amazonas(6,000)", "[bwi]" },
	{ "BNJ", "Banjari / Banjara / Gormati / Lambadi : India(4m)", "[lmn]" },
	{ "BNT", "Bantawa : Nepal(400,000)", "[bap]" },
	{ "BON", "Bondo : India - Odisha(9000)", "[bfw]" },
	{ "BOR", "Boro / Bodo : India - Assam,W Bengal(1.3m)", "[brx]" },
	{ "BOS", "Bosnian(derived from Serbocroat) : Bosnia - Hercegovina(2m)", "[bos]" },
	{ "BR", "Burmese / Barma / Myanmar : Myanmar(32m)", "[mya]" },
	{ "BRA", "Brahui : Pakistan(4m), Afghanistan(0.2m)", "[brh]" },
	{ "BRB", "Bariba / Baatonum : Benin(0.5m), Nigeria(0.1m)", "[bba]" },
	{ "BRU", "Bru : Laos(30,000), Vietnam(55,000)", "[bru]" },
	{ "BSL", "Bislama : Vanuatu(10,000)", "[bis]" },
	{ "BT", "Black Tai / Tai Dam : Vietnam(0.7m)", "[blt]" },
	{ "BTK", "Batak - Toba : Indonesia - Sumatra(2m)", "[bbc]" },
	{ "BU", "Bulgarian : Bulgaria(6m)", "[bul]" },
	{ "BUG", "Bugis / Buginese : Indonesia - Sulawesi(5m)", "[bug]" },
	{ "BUK", "Bukharian / Bukhori : Israel(50,000), Uzbekistan(10,000)", "[bhh]" },
	{ "BUN", "Bundeli / Bundelkhandi / Bundelkandi : India - Uttar,Madhya Pr. (3m)", "[bns]" },
	{ "BUR", "Buryat : Russia - Buryatia, Lake Baikal(0.4m)", "[bxr]" },
	{ "BY", "Byelorussian / Belarusian : Belarus, Poland, Ukraine(8m)", "[bel]" },
	{ "C", "Chinese(not further specified)", "[]" },
	{ "CA", "Cantonese / Yue : China - Guangdong(50m),Hongkong(6m),Malaysia(1m)", "[yue]" },
	{ "CC", "Chaochow(dialect of Min - Nan) : China - Guangdong(10m), Thailand(1m)", "[nan]" },
	{ "CD", "Chowdary / Chaudhry / Chodri : India - Gujarat(0.2m)", "[cdi]" },
	{ "CEB", "Cebuano : Philippines(16m)", "[ceb]" },
	{ "CH", "Chin(not further specified) : Myanmar; includes those below a.o.", "[]" },
	{ "C-A", "Chin - Asho: Myanmar - Ayeyarwady,Rakhine(30,000)", "[csh]" },
	{ "C-D", "Chin - Daai : Myanmar - Chin(37,000)", "[dao]" },
	{ "C-F", "Chin - Falam / Halam : Myanmar - Chin, Bangladesh, India(0.1m)", "[cfm]" },
	{ "C-H", "Chin - Haka : Myanmar - Chin(100,000)", "[cnh]" },
	{ "CHA", "Cha'palaa / Chachi: Ecuador-Esmeraldas (10,000)", "[cbi]" },
	{ "CHE", "Chechen : Russia - Chechnya(1.4m)", "[che]" },
	{ "CHG", "Chhattisgarhi : India - Chhattisgarh, Odisha, Bihar(13m)", "[hne]" },
	{ "CHI", "Chitrali / Khowar : NW Pakistan(0.2m)", "[khw]" },
	{ "C-K", "Chin - Khumi : Myanmar - Chin,Rakhine(0.6m)", "[cnk]" },
	{ "C-M", "Chin - Mro : Myanmar - Rakhine,Chin(75,000)", "[cmr]" },
	{ "C-O", "Chin - Thado / Thadou - Kuki : India - Assam, Manipur(0.2m)", "[tcz]" },
	{ "CHR", "Chrau : Vietnam(7,000)", "[crw]" },
	{ "CHU", "Chuwabu : Mozambique(1m)", "[chw]" },
	{ "C-T", "Chin - Tidim : Myanmar - Chin(0.2m), India - Mizoram,Manipur(0.15m)", "[ctd]" },
	{ "C-Z", "Chin - Zomin / Zomi - Chin : Myanmar(60,000), India - Manipur(20,000)", "[zom]" },
	{ "CKM", "Chakma : India - Mizoram,Tripura,Assam(0.2m), Bangladesh(0.15m)", "[ccp]" },
	{ "CKW", "Chokwe : Angola(0.5m), DR Congo(0.5m)", "[cjk]" },
	{ "COF", "Cofan / Cofán : Ecuador - Napo, Colombia(2000)", "[con]" },
	{ "COK", "Cook Islands Maori / Rarotongan : Cook Islands(13,000)", "[rar]" },
	{ "CR", "Creole / Haitian : Haiti(7m)", "[hat]" },
	{ "CRU", "Chru : Vietnam(19,000)", "[cje]" },
	{ "CT", "Catalan : Spain(7m), Andorra(31,000)", "[cat]" },
	{ "CV", "Chuvash : Russia - Chuvashia(1m)", "[chv]" },
	{ "CW", "Chewa / Chichewa / Nyanja / Chinyanja : Malawi(7m), MOZ(0.6m),ZMB(0.8m)", "[nya]" },
	{ "CZ", "Czech : Czech Republic(9m)", "[ces]" },
	{ "D", "German : Germany(80m), Austria, Switzerland, Belgium", "[deu]" },
	{ "D-P", "Lower German(varieties in N.Germany, USA:Pennsylvania Dutch)", "[pdc]" },
	{ "DA", "Danish : Denmark(5.5m)", "[dan]" },
	{ "DAO", "Dao : Vietnam ethnic group speaking MIE and Kim Mun(0.7m)", "[]" },
	{ "DAR", "Dargwa / Dargin : Russia - Dagestan(0.5m)", "[dar]" },
	{ "DD", "Dhodiya / Dhodia : India - Gujarat(150,000)", "[dho]" },
	{ "DEC", "Deccan / Deccani / Desi : India - Maharashtra(13m)", "[dcc]" },
	{ "DEG", "Degar / Montagnard(Vietnam) : comprises JR, RAD, BHN, KOH, MNO, STI", "[]" },
	{ "DEN", "Dendi : Benin(30,000)", "[ddn]" },
	{ "DEO", "Deori : India - Assam(27,000)", "[der]" },
	{ "DES", "Desiya / Deshiya : India - Odisha(50,000)", "[dso]" },
	{ "DH", "Dhivehi : Maldives(0.3m)", "[div]" },
	{ "DI", "Dinka : South Sudan(1.4m)", "[dip,diw,dik,dib,dks]" },
	{ "DIM", "Dimasa / Dhimasa : India - Assam : (0.1m)", "[dis]" },
	{ "DIT", "Ditamari : Benin(0.1m)", "[tbz]" },
	{ "DO", "Dogri - Kangri : N India(4m)", "[doi]" },
	{ "DR", "Dari / Eastern Farsi : Afghanistan(7m), Pakistan(2m)", "[prs]" },
	{ "DU", "Dusun : Malaysia - Sabah(0.1m)", "[dtp]" },
	{ "DUN", "Dungan : Kyrgyzstan(40,000)", "[dng]" },
	{ "DY", "Dyula / Jula : Burkina Faso(1m), Ivory Coast(1.5m), Mali(50,000)", "[dyu]" },
	{ "DZ", "Dzongkha : Bhutan(0.2m)", "[dzo]" },
	{ "E", "English : UK(60m), USA(225m), India(200m), others", "[eng]" },
	{ "EC", "Eastern Cham : Vietnam(70,000)", "[cjm]" },
	{ "EGY", "Egyptian Arabic : Egypt(52m)", "[arz]" },
	{ "EO", "Esperanto : Constructed language(2m)", "[epo]" },
	{ "ES", "Estonian : Estonia(1m)", "[ekk]" },
	{ "EWE", "Ewe / Éwé : Ghana(2m), Togo(1m)", "[ewe]" },
	{ "F", "French : France(53m), Canada(7m), Belgium(4m), Switzerland(1m)", "[fra]" },
	{ "FA", "Faroese : Faroe Islands(66,000)", "[fao]" },
	{ "FI", "Finnish : Finland(5m)", "[fin]" },
	{ "FJ", "Fijian : Fiji(0.3m)", "[fij]" },
	{ "FON", "Fon / Fongbe : Benin(1.4m)", "[fon]" },
	{ "FP", "Filipino(based on Tagalog) : Philippines(25m)", "[fil]" },
	{ "FS", "Farsi / Iranian Persian : Iran(45m)", "[pes]" },
	{ "FT", "Fiote / Vili : Rep.Congo(7000), Gabon(4000)", "[vif]" },
	{ "FU", "Fulani / Fulfulde : Nigeria(8m), Niger(1m),Burkina Faso(1m)", "[fub,fuh,fuq]" },
	{ "FUJ", "FutaJalon / Pular : Guinea(3m)", "[fuf]" },
	{ "FUR", "Fur : Sudan - Darfur(0.5m)", "[fvr]" },
	{ "GA", "Garhwali : India - Uttarakhand,Himachal Pr. (3m)", "[gbm]" },
	{ "GAG", "Gagauz : Moldova(0.1m)", "[gag]" },
	{ "GAR", "Garo : India - Meghalaya,Assam,Nagaland,Tripura(1m)", "[grt]" },
	{ "GD", "Greenlandic Inuktikut : Greenland(50,000)", "[kal]" },
	{ "GE", "Georgian : Georgia(4m)", "[kat]" },
	{ "GI", "Gilaki : Iran(3m)", "[glk]" },
	{ "GJ", "Gujari / Gojri : NW India(0.7m), Pakistan(0.3m)", "[gju]" },
	{ "GL", "Galicic / Gallego : Spain(3m)", "[glg]" },
	{ "GM", "Gamit : India - Gujarat(0.3m)", "[gbl]" },
	{ "GNG", "Gurung(Eastern and Western) : Nepal(0.4m)", "[ggn,gvr]" },
	{ "GO", "Gorontalo : Indonesia - Sulawesi(1m)", "[gor]" },
	{ "GON", "Gondi : India - Madhya Pr.,Maharashtra(2m)", "[gno]" },
	{ "GR", "Greek : Greece(10m), Cyprus(0.7m)", "[ell]" },
	{ "GU", "Gujarati : India - Gujarat,Maharashtra,Rajasthan(46m)", "[guj]" },
	{ "GUA", "Guaraní : Paraguay(5m)", "[grn]" },
	{ "GUR", "Gurage / Guragena : Ethiopia(0.4m)", "[sgw]" },
	{ "GZ", "Ge'ez / Geez (liturgic language of Ethiopia)", "[gez]" },
	{ "HA", "Haussa : Nigeria(19m), Niger(5m), Benin(1m)", "[hau]" },
	{ "HAD", "Hadiya : Ethiopia(1.2m)", "[hdy]" },
	{ "HAR", "Haryanvi / Bangri / Harayanvi / Hariyanvi : India - Haryana(8m)", "[bgc]" },
	{ "HAS", "Hassinya / Hassaniya : Mauritania(3m)", "[mey]" },
	{ "HB", "Hebrew : Israel(5m)", "[heb]" },
	{ "HD", "Hindko(Northern and Southern) : Pakistan(3m)", "[hnd,hno]" },
	{ "HI", "Hindi : India(260m)", "[hin]" },
	{ "HK", "Hakka : South China(26m), Taiwan(3m), Malaysia(1m)", "[hak]" },
	{ "HM", "Hmong / Miao languages : S China, N Vietnam, N Laos, USA(3m)", "[hmn]" },
	{ "HMA", "Hmar : India - Assam,Manipur,Mizoram(80,000)", "[hmr]" },
	{ "HMB", "Hmong - Blue / Njua : Laos(0.1m), Thailand(60,000)", "[hnj]" },
	{ "HMQ", "Hmong, Northern Qiandong / Black Hmong : S China(1m)", "[hea]" },
	{ "HMW", "Hmong - White / Daw : Vietnam(1m), Laos(0.2m), S China(0.2m)", "[mww]" },
	{ "HN", "Hani : China - Yunnan(0.7m)", "[hni]" },
	{ "HO", "Ho : India - Jharkand,Odisha,W Bengal(1m)", "[hoc]" },
	{ "HR", "Croatian / Hrvatski : Croatia(4m), BIH(0.5m), Serbia(0.1m)", "[hrv]" },
	{ "HRE", "Hre : Vietnam(0.1m)", "[hre]" },
	{ "HU", "Hungarian : Hungary(10m), Romania(1.5m), SVK(0.5m), SRB(0.3m)", "[hun]" },
	{ "HUI", "Hui / Huizhou : China - Anhui,Zhejiang(5m)", "[czh]" },
	{ "HZ", "Hazaragi : Afghanistan(1.8m), Iran(0.3m)", "[haz]" },
	{ "I", "Italian : Italy(55m), Switzerland(0.5m), San Marino(25,000)", "[ita]" },
	{ "IB", "Iban : Malaysia - Sarawak(0.7m)", "[iba]" },
	{ "IF", "Ifè / Ife : Togo(0.1m), Benin(80,000)", "[ife]" },
	{ "IG", "Igbo / Ibo : Nigeria(18m)", "[ibo]" },
	{ "ILC", "Ilocano : Philippines(7m)", "[ilo]" },
	{ "ILG", "Ilonggo : Philippines(6m)", "[hil]" },
	{ "IN", "Indonesian / Bahasa Indonesia : Indonesia(140m)", "[ind]" },
	{ "INU", "Inuktikut : Canada - Nunavut,N Quebec,Labrador(30,000)", "[ike]" },
	{ "IRQ", "Iraqi Arabic : Iraq(12m), Iran(1m), Syria(2m)", "[acm]" },
	{ "IS", "Icelandic : Iceland(0.2m)", "[isl]" },
	{ "ISA", "Isan / Northeastern Thai : Thailand(15m)", "[tts]" },
	{ "J", "Japanese : Japan(122m)", "[jpn]" },
	{ "JEH", "Jeh : Vietnam(15,000), Laos(8,000)", "[jeh]" },
	{ "JG", "Jingpho", "[]" },
	{ "JOR", "Jordanian Arabic : Jordan(3.5m), Israel / Palestine(2.5m)", "[ajp]" },
	{ "JR", "Jarai / Giarai / Jra : Vietnam(0.3m)", "[jra]" },
	{ "JU", "Juba Arabic : South Sudan(60,000)", "[pga]" },
	{ "JV", "Javanese : Indonesia - Java,Bali(84m)", "[jav]" },
	{ "K", "Korean : Korea(62m), China - Jilin,Heilongjiang,Liaoning(2m)", "[kor]" },
	{ "KA", "Karen(unspecified) : Myanmar(3m)", "[]" },
	{ "K-G", "Karen - Geba : Myanmar(40,000)", "[kvq]" },
	{ "K-K", "Karen - Geko / Gekho : Myanmar(17,000)", "[ghk]" },
	{ "K-P", "Karen - Pao / Black Karen / Pa'o: Myanmar (0.5m)", "[blk]" },
	{ "K-S", "Karen - Sgaw / S'gaw: Myanmar (1.3m), Thailand (0.2m)", "[ksw]" },
	{ "K-W", "Karen - Pwo : Myanmar(1m); Northern variant : Thailand(60,000)", "[kjp,pww]" },
	{ "KAD", "Kadazan : Malaysia - Sabah(80,000)", "[kzj,dtb]" },
	{ "KAL", "Kalderash Romani(Dialect of Vlax) : Romania(0.2m)", "[rmy]" },
	{ "KAB", "Kabardian : Russia - Caucasus(0.5m), Turkey(1m)", "[kbd]" },
	{ "KAM", "Kambaata : Ethiopia(0.6m)", "[ktb]" },
	{ "KAN", "Kannada : India - Karnataka,Andhra Pr.,Tamil Nadu(40m)", "[kan]" },
	{ "KAO", "Kaonde : Zambia(0.2m)", "[kqn]" },
	{ "KAR", "Karelian : Russia - Karelia(25,000), Finland(10,000)", "[krl]" },
	{ "KAT", "Katu : Vietnam(50,000)", "[ktv]" },
	{ "KAU", "Kau Bru / Riang : India - Tripura,Mizoram,Assam(77,000)", "[ria]" },
	{ "KAY", "Kayan : Myanmar(0.1m)", "[pdu]" },
	{ "KB", "Kabyle : Algeria(5m)", "[kab]" },
	{ "KBO", "Kok Borok / Tripuri : India(0.8m)", "[trp]" },
	{ "KC", "Kachin / Jingpho : Myanmar(0.9m)", "[kac]" },
	{ "KG", "Kyrgyz / Kirghiz : Kyrgystan(2.5m), China(0.1m)", "[kir]" },
	{ "KH", "Khmer : Cambodia(13m), Vietnam(1m)", "[khm]" },
	{ "KHA", "Kham / Khams, Eastern : China - NE Tibet(1.4m)", "[khg]" },
	{ "KHM", "Khmu : Laos(0.6m)", "[kjg]" },
	{ "KHR", "Kharia / Khariya : India - Jharkand(0.2m)", "[khr]" },
	{ "KHS", "Khasi / Kahasi : India - Meghalaya,Assam(0.8m)", "[kha]" },
	{ "KHT", "Khota(India)", "[]" },
	{ "KIM", "Kimwani : Mozambique(0.1m)", "[wmw]" },
	{ "KIN", "Kinnauri / Kinori : India - Himachal Pr. (65,000)", "[kfk]" },
	{ "KiR", "KiRundi : Burundi(9m)", "[run]" },
	{ "KK", "KiKongo / Kongo : DR Congo, Angola(8m)", "[kng]" },
	{ "KKN", "Kukna : India - Gujarat(0.1m)", "[kex]" },
	{ "KMB", "Kimbundu / Mbundu / Luanda : Angola(4m)", "[kmb]" },
	{ "KMY", "Kumyk : Russia - Dagestan(0.4m)", "[kum]" },
	{ "KND", "Khandesi : India - Maharashtra(22,000)", "[khn]" },
	{ "KNK", "KinyaRwanda - KiRundi service of the Voice of America / BBC", "[]" },
	{ "KNU", "Kanuri : Nigeria(3.2m), Chad(0.1m), Niger(0.4m)", "[kau]" },
	{ "KNY", "Konyak Naga : India - Assam,Nagaland(0.25m)", "[nbe]" },
	{ "KOH", "Koho / Kohor : Vietnam(0.2m)", "[kpm]" },
	{ "KOK", "Kokang Shan : Myanmar(dialect of Shan)", "[]" },
	{ "KOM", "Komering : Indonesia - Sumatera(0.5m)", "[kge]" },
	{ "KON", "Konkani : India - Maharashtra,Karnataka,Kerala(2.4m)", "[knn]" },
	{ "KOT", "Kotokoli / Tem : Togo(0.2m), Benin(0.05m), Ghana(0.05m)", "[kdh]" },
	{ "KOY", "Koya : India - Andhra Pr.,Odisha(0.4m)", "[kff]" },
	{ "KPK", "Karakalpak : W Uzbekistan(0.4m)", "[kaa]" },
	{ "KRB", "Karbi / Mikir / Manchati : India - Assam,Arunachal Pr. (0.4m)", "[mjw]" },
	{ "KRI", "Krio : Sierra Leone(0.5m)", "[kri]" },
	{ "KRW", "KinyaRwanda : Rwanda(7m), Uganda(0.4m), DR Congo(0.2m)", "[kin]" },
	{ "KS", "Kashmiri : India(5m), Pakistan(0.1m)", "[kas]" },
	{ "KT", "Kituba(simplified Kikongo) : DR Congo(4m)", "[ktu]" },
	{ "KTW", "Kotwali(dialect of Bhili) : India - Gujarat,Maharshtra", "[bhb]" },
	{ "KU", "Kurdish : Turkey(15m), Iraq(6.3m), Iran(6.5m), Syria(1m)", "[ckb,kmr,sdh]" },
	{ "KuA", "Kurdish and Arabic", "[]" },
	{ "KuF", "Kurdish and Farsi", "[]" },
	{ "KUI", "Kui : India - Odisha,Ganjam,Andhra Pr. (1m)", "[kxu]" },
	{ "KUL", "Kulina : Brazil - Acre(3500)", "[cul]" },
	{ "KUM", "Kumaoni / Kumauni : India - Uttarakhand(2m)", "[kfy]" },
	{ "KUN", "Kunama : Eritrea(0.2m)", "[kun]" },
	{ "KUP", "Kupia / Kupiya : India - Andhra Pr. (6,000)", "[key]" },
	{ "KUR", "Kurukh / Kurux : India - Chhatisgarh,Jharkhand,W.Bengal(2m)", "[kru]" },
	{ "KUs", "Sorani(Central) Kurdish : Iraq(3.5m), Iran(3.3m)", "[ckb]" },
	{ "KUT", "Kutchi : India - Gujarat(0.4m), Pakistan - Sindh(0.2m)", "[gjk]" },
	{ "KUV", "Kuvi : India - Odisha(0.16m)", "[kxv]" },
	{ "KVI", "Kulluvi / Kullu : India - Himachal Pr. (0.1m)", "[kfx]" },
	{ "KWA", "Kwanyama / Kuanyama(dialect of OW) : Angola(0.4m), Namibia(0.2m)", "[kua]" },
	{ "KYH", "Kayah : Myanmar(0.15m)", "[kyu]" },
	{ "KZ", "Kazakh : Kazakhstan(7m), China(1m), Mongolia(0.1m)", "[kaz]" },
	{ "L", "Latin : Official language of Catholic church", "[lat]" },
	{ "LA", "Ladino", "[]" },
	{ "LAD", "Ladakhi / Ladak : India - Jammu and Kashmir(0.1m)", "[lbj]" },
	{ "LAH", "Lahu : China(0.3m), Myanmar(0.2m)", "[lhu]" },
	{ "LAK", "Lak : Russia - Dagestan(0.15m)", "[lbe]" },
	{ "LAM", "Lampung : Indonesia - Sumatera(1m)", "[abl,ljp]" },
	{ "LAO", "Lao : Laos(3m)", "[lao]" },
	{ "LB", "Lun Bawang / Murut : Malaysia - Sarawak(24,000), Indonesia(23,000)", "[lnd]" },
	{ "LBN", "Lebanon Arabic(North Levantine) : Lebanon(4m), Syria(9m)", "[apc]" },
	{ "LBO", "Limboo / Limbu : Nepal(0.3m), India - Sikkim,W.Bengal,Assam(40,000)", "[lif]" },
	{ "LEP", "Lepcha : India - Sikkim,W.Bengal(50,000)", "[lep]" },
	{ "LEZ", "Lezgi : Russia - Dagestan(0.4m), Azerbaijan(0.4m)", "[lez]" },
	{ "LIM", "Limba : Sierra Leone(0.3m)", "[lia]" },
	{ "LIN", "Lingala : DR Congo(2m), Rep.Congo(0.1m)", "[lin]" },
	{ "LIS", "Lisu : China - West Yunnan(0.6m), Burma(0.3m)", "[lis]" },
	{ "LND", "Lunda(see LU), in particular its dialect Ndembo : Angola(0.2m)", "[lun]" },
	{ "LNG", "Lungeli Magar(possibly same as MGA ? )", "[]" },
	{ "LO", "Lomwe / Ngulu : Mocambique(1.5m)", "[ngl]" },
	{ "LOK", "Lokpa / Lukpa : Benin(50,000), Togo(14,000)", "[dop]" },
	{ "LOZ", "Lozi / Silozi : Zambia(0.6m), ZWE(70,000), NMB - E Caprivi(30,000)", "[loz]" },
	{ "LT", "Lithuanian : Lithuania(3m)", "[lit]" },
	{ "LTO", "Oriental Liturgy of Vaticane Radio" , "[]" },
	{ "LU", "Lunda : Zambia(0.5m)", "[lun]" },
	{ "LUB", "Luba : DR Congo - Kasai(6m)", "[lua]" },
	{ "LUC", "Luchazi : Angola(0.4m), Zambia(0.05m)", "[lch]" },
	{ "LUG", "Luganda : Uganda(4m)[lug]" },
	{ "LUN", "Lunyaneka / Nyaneka : Angola(0.3m)", "[nyk]" },
	{ "LUR", "Luri, Northern and Southern : Iran(1.5m and 0.9m)", "[lrc,luz]" },
	{ "LUV", "Luvale : Angola(0.5m), Zambia(0.2m)", "[lue]" },
	{ "LV", "Latvian : Latvia(1.2m)", "[lvs]" },
	{ "M", "Mandarin(Standard Chinese / Beijing dialect) : China(840m)", "[cmn]" },
	{ "MA", "Maltese : Malta(0.3m)", "[mlt]" },
	{ "MAD", "Madurese / Madura : Indonesia - Java(7m)", "[mad]" },
	{ "MAG", "Maghi / Magahi / Maghai : India - Bihar,Jharkhand(14m)", "[mag]" },
	{ "MAI", "Maithili / Maithali : India - Bihar(30m), Nepal(3m)", "[mai]" },
	{ "MAK", "Makonde : Tanzania(1m), Mozambique(0.4m)", "[kde]" },
	{ "MAL", "Malayalam : India - Kerala(33m)", "[mal]" },
	{ "MAM", "Maay / Mamay / Rahanweyn : Somalia(2m)", "[ymm]" },
	{ "MAO", "Maori : New Zealand(60,000)", "[mri]" },
	{ "MAR", "Marathi : India - Maharashtra(72m)", "[mar]" },
	{ "MAS", "Maasai / Massai / Masai : Kenya(0.8m), Tanzania(0.5m)", "[mas]" },
	{ "MC", "Macedonian : Macedonia(1.4m), Albania(0.1m)", "[mkd]" },
	{ "MCH", "Mavchi / Mouchi / Mauchi / Mawchi : India - Gujarat,Maharashtra(0.1m)", "[mke]" },
	{ "MEI", "Meithei / Manipuri / Meitei : India - Manipur,Assam(1.5m)", "[mni]" },
	{ "MEN", "Mende : Sierra Leone(1.5m)", "[men]" },
	{ "MEW", "Mewari / Mewadi(a Rajasthani variety) : India - Rajasthan(5m)", "[mtr]" },
	{ "MGA", "Magar(Western and Eastern) : Nepal(0.8m)", "[mrd,mgp]" },
	{ "MIE", "Mien / Iu Mien : S China(0.4m), Vietnam(0.4m)", "[ium]" },
	{ "MIS", "Mising : India - Assam,Arunachal Pr. (0.5m)", "[mrg]" },
	{ "MKB", "Minangkabau : Indonesia - West Sumatra(5.5m)", "[min]" },
	{ "MKS", "Makassar / Makasar : Indonesia - South Sulawesi(2m)", "[mak]" },
	{ "MKU", "Makua / Makhuwa : Mocambique(3m)", "[vmw]" } ,
	{ "ML", "Malay / Baku : Malaysia(10m), Singapore(0.4m), Indonesia(5m)", "[zsm,zlm]" },
	{ "MLK", "Malinke / Maninka(We / Ea) : Guinea(3m), SEN(0.4m), Mali(0.8m)", "[emk,mlq]" },
	{ "MLT", "Malto / Kumarbhag Paharia : India - Jharkhand(12,000)", "[kmj]" },
	{ "MNA", "Mina / Gen : Togo(0.2m), Benin(0.1m)", "[gej]" },
	{ "MNE", "Montenegrin(quite the same as SR) : Montenegro(0.2m)", "[srp]" },
	{ "MNO", "Mnong(Ea,Ce,So) : Vietnam(90,000), Cambodia(40,000)", "[mng,cmo,mnn]" },
	{ "MO", "Mongolian : Mongolia(Halh; 2m), China(Peripheral; 3m)", "[khk,mvf]" },
	{ "MON", "Mon : Myanmar - Mon,Kayin(0.7m), Thailand(0.1m)", "[mnw]" },
	{ "MOO", "Moore / Mòoré : Burkina Faso(5m)", "[mos]" },
	{ "MOR", "Moro / Moru / Muro : Sudan - S Korodofan(30,000)", "[mor]" },
	{ "MR", "Maronite / Cypriot Arabic : Cyprus(1300)", "[acy]" },
	{ "MRC", "Moroccan / Mugrabian Arabic : Morocco(20m)", "[ary]" },
	{ "MRI", "Mari : Russia - Mari(0.8m)", "[chm]" },
	{ "MRU", "Maru / Lhao Vo : Burma - Kachin,Shan(0.1m)", "[mhx]" },
	{ "MSY", "Malagasy : Madagaskar(16m)", "[mlg]" },
	{ "MUN", "Mundari : India - Jharkhand,Odisha(1.1m)", "[unr]" },
	{ "MUO", "Muong : Vietnam(1m)", "[mtq]" },
	{ "MUR", "Murut : Malaysia - Sarawak,Sabah(4500)", "[kxi,mvv,tih]" },
	{ "MW", "Marwari(a Rajasthani variety) : India - Rajasthan,Gujarat(6m)", "[rwr]" },
	{ "MX", "Macuxi / Macushi : Brazil(16,000), Guyana(1,000)", "[mbc]" },
	{ "MY", "Maya(Yucatec) : Mexico(0.7m), Belize(6000)", "[yua]" },
	{ "MZ", "Mizo / Lushai : India - Mizoram(0.7m)", "[lus]" },
	{ "NAG", "Naga(var.incl.Ao,Makware) : India - Nagaland, Assam(2m)", "[njh,njo,nmf,nph]" },
	{ "NDA", "Ndau : Mocambique(1.6m), Zimbabwe(0.8m)", "[ndc]" },
	{ "NDE", "Ndebele : Zimbabwe(1.5m), South Africa - Limpopo(0.6m)", "[nde,nbl]" },
	{ "NE", "Nepali / Lhotshampa : Nepal(11m), India(3m), Bhutan(0.1m)", "[npi]" },
	{ "NG", "Nagpuri / Sadani / Sadari / Sadri : India - Jharkhand,W.Bengal(3m)", "[sck]" },
	{ "NGA", "Ngangela / Nyemba : Angola(0.2m)", "[nba]" },
	{ "NIC", "Nicobari : India - Nicobar Islands(40,000)", "[caq]" },
	{ "NIS", "Nishi / Nyishi : India - Arunachal Pradesh(0.2m)", "[njz]" },
	{ "NIU", "Niuean : Niue(2,000)", "[niu]" },
	{ "NL", "Dutch : Netherlands(16m), Belgium(6m), Suriname(0.2m)", "[nld]" },
	{ "NLA", "Nga La / Matu Chin : Myanmar - Chin(30,000), India - Mizoram(10,000)", "[hlt]" },
	{ "NO", "Norwegian : Norway(5m)", "[nor]" },
	{ "NOC", "Nocte / Nockte : India - Assam,Arunachal Pr. (33,000)", "[njb]" },
	{ "NP", "Nupe : Nigeria(0.8m)", "[nup]" },
	{ "NTK", "Natakani / Netakani / Varhadi - Nagpuri : India - Maharashtra,M.Pr. (7m)", "[vah]" },
	{ "NU", "Nuer : Sudan(0.8m), Ethiopia(0.2m)", "[nus]" },
	{ "NUN", "Nung : Vietnam(1m)", "[nut]" },
	{ "NW", "Newar / Newari : Nepal(0.8m)", "[new]" },
	{ "NY", "Nyanja", "[]" },
	{ "OG", "Ogan : Indonesia - Sumatera(less than 0.5m)", "[pse]" },
	{ "OH", "Otjiherero service in Namibia(Languages : Herero, SeTswana)", "[]" },
	{ "OO", "Oromo : Ethiopia(26m)", "[orm]" },
	{ "OR", "Odia / Oriya / Orissa : India - Odisha,Chhattisgarh(32m)", "[ory]" },
	{ "OS", "Ossetic : Russia(0.5m), Georgia(0.1m)", "[oss]" },
	{ "OW", "Oshiwambo service in Angola and Namibia(Languages : Ovambo, Kwanyama)", "[]" },
	{ "P", "Portuguese : Brazil(187m), Angola(14m), Portugal(10m)", "[por]" },
	{ "PAL", "Palaung - Pale : Myanmar(0.3m)", "[pce]" },
	{ "PAS", "Pasemah : Indonesia - Sumatera(less than 0.5m)", "[pse]" },
	{ "PED", "Pedi : S Africa(4m)", "[nso]" },
	{ "PJ", "Punjabi : Pakistan(60m), India - Punjab,Rajasthan(28m)", "[pnb,pan]" },
	{ "PO", "Polish : Poland(37m)", "[pol]" },
	{ "POR", "Po : Myanmar - Rakhine", "[]" },
	{ "POT", "Pothwari : Pakistan(2.5m)", "[phr]" },
	{ "PS", "Pashto / Pushtu : Afghanistan(6m), Pakistan(1m)", "[pbt]" },
	{ "PU", "Pulaar : Senegal(3m), Gambia(0.3m)", "[fuc]" },
	{ "Q", "Quechua : Peru, Bolivia, Ecuador(various varieties; 9m)", "[que,qvi]" },
	{ "QQ", "Qashqai : Iran(1.5m)", "[qxq]" },
	{ "R", "Russian : Russia(137m), Ukraine(8m), Kazakhstan(6m), Belarus(1m)", "[rus]" },
	{ "RAD", "Rade / Ede : Vietnam(0.2m)", "[rad]" },
	{ "REN", "Rengao : Vietnam(18,000)", "[ren]" },
	{ "RGM", "Rengma Naga : India - Nagaland(34,000)", "[nre,nnl]" },
	{ "RO", "Romanian : Romania(20m), Moldova(3m), Serbia - Vojvodina(0.2m)", "[ron]" },
	{ "ROG", "Roglai(Northern, Southern) : Vietnam(0.1m)", "[rog,rgs]" },
	{ "RON", "Rongmei Naga : India - Manipur,Nagaland,Assam(60,000)", "[nbu]" },
	{ "Ros", "Rosary session of Vaticane Radio", "[]" },
	{ "RU", "Rusyn / Ruthenian : Ukraine(0.5m), Serbia - Vojvodina(30,000)", "[rue]" },
	{ "RWG", "Rawang : Myanmar - Kachin(60,000)", "[raw]" },
	{ "S", "Spanish / Castellano : Spain(30m), Latin America(336m), USA(34m)", "[spa]" },
	{ "SAH", "Saho : Eritrea(0.2m)", "[ssy]" },
	{ "SAN", "Sango : Central African Rep. (0.4m)", "[sag]" },
	{ "SAR", "Sara / Sar : Chad(0.2m)", "[mwm]" },
	{ "SAS", "Sasak : Indonesia - Lombok(2m)", "[sas]" },
	{ "SC", "Serbocroat(Yugoslav language up to national / linguistic separation)", "[hbs]" },
	{ "SCA", "Scandinavian languages(Norwegian, Swedish, Finnish)", "[]" },
	{ "SD", "Sindhi : Pakistan(19m), India(2m)", "[snd]" },
	{ "SED", "Sedang : Vietnam(0.1m)", "[sed]" },
	{ "SEF", "Sefardi / Judeo Spanish / Ladino : Israel(0.1m), Turkey(10,000)", "[lad]" },
	{ "SEN", "Sena : Mocambique(1m)", "[seh]" },
	{ "SFO", "Senoufo / Sénoufo - Syenara : Mali(0.15m)", "[shz]" },
	{ "SGA", "Shangaan / Tsonga : Mocambique(2m), South Africa(2m)", "[tso]" },
	{ "SGM", "Sara Gambai / Sara Ngambai : Chad(0.9m)", "[sba]" },
	{ "SGO", "Songo : Angola(50,000)", "[nsx]" },
	{ "SGT", "Sangtam : India - Nagaland(84,000)", "[nsa]" },
	{ "SHA", "Shan : Myanmar(3m)", "[shn]" },
	{ "SHk", "Shan - Khamti : Myanmar(8,000), India - Assam(5,000)", "[kht]" },
	{ "SHC", "Sharchogpa / Sarchopa / Tshangla : E Bhutan(0.14m)", "[tsj]" },
	{ "SHE", "Sheena / Shina : Pakistan(0.6m)", "[scl,plk]" },
	{ "SHK", "Shiluk / Shilluk : South Sudan(0.2m)", "[shk]" },
	{ "SHO", "Shona : Zimbabwe(11m)", "[sna]" },
	{ "SHP", "Sherpa : Nepal(0.1m)", "[xsr]" },
	{ "SHU", "Shuwa Arabic : Chad(1m), Nigeria(0.1m), N Cameroon(0.1m)", "[shu]" },
	{ "SI", "Sinhalese / Sinhala : Sri Lanka(16m)", "[sin]" },
	{ "SID", "Sidamo / Sidama : Ethiopia(3m)", "[sid]" },
	{ "SIK", "Sikkimese / Bhutia : India - Sikkim,W.Bengal(70,000)", "[sip]" },
	{ "SIR", "Siraiki / Seraiki : Pakistan(14m)", "[skr]" },
	{ "SK", "Slovak : Slovakia(5m), Czech Republic(0.2m), Serbia(80,000)", "[slk]" },
	{ "SLM", "Pijin / Solomon Islands Pidgin : Solomon Islands(0.3m)", "[pis]" },
	{ "SLT", "Silte / East Gurage : Ethiopia(1m)", "[stv]" },
	{ "SM", "Samoan : Samoa(0.2m), American Samoa(0.05m)", "[smo]" },
	{ "SMP", "Sambalpuri / Sambealpuri : India - Odisha,Chhattisgarh(18m)", "[spv]" },
	{ "SNK", "Sanskrit : India(0.2m)", "[san]" },
	{ "SNT", "Santhali : India - Bihar,Jharkhand,Odisha(6m), Bangladesh(0.2m)", "[sat]" },
	{ "SO", "Somali : Somalia(8m), Ethiopia(5m), Kenya(2m), Djibouti(0.3m)", "[som]" },
	{ "SON", "Songhai : Mali(0.6m)", "[ses,khq]" },
	{ "SOT", "SeSotho : South Africa(4m), Lesotho(2m)", "[sot]" },
	{ "SR", "Serbian : Serbia(7m), Bosnia - Hercegovina(1.5m)", "[srp]" },
	{ "SRA", "Soura / Sora : India - Odisha,Andhra Pr. (0.3m)", "[srb]" },
	{ "STI", "Stieng : Vietnam(85,000)", "[sti,stt]" },
	{ "SUA", "Shuar : Ecuador(35,000)", "[jiv]" },
	{ "SUD", "Sudanese Arabic : Sudan and South Sudan(15m)", "[apd]" },
	{ "SUM", "Sumi Naga : India - Nagaland(0.1m)", "[nsm]" },
	{ "SUN", "Sunda / Sundanese : Indonesia - West Java(34m)", "[sun]" },
	{ "SV", "Slovenian : Slovenia(1.7m), Italy(0.1m), Austria(18,000)", "[slv]" },
	{ "SWA", "Swahili / Kisuaheli : Tanzania(15m), Kenya, Ea.DR Congo(9m)", "[swc,swh]" },
	{ "SWE", "Swedish : Sweden(8m), Finland(0.3m)", "[swe]" },
	{ "SWZ", "SiSwati : Swaziland(1m), South Africa(1m)", "[ssw]" },
	{ "T", "Thai : Thailand(20m)", "[tha]" },
	{ "TAG", "Tagalog : Philippines(22m)", "[tgl]" },
	{ "TAH", "Tachelhit / Sous : Morocco, southern(4m), Algeria", "[shi]" },
	{ "TAM", "Tamil : S.India(60m), Malaysia(4m), Sri Lanka(4m)", "[tam]" },
	{ "TB", "Tibetan : Tibet(1m), India(0.1m)", "[bod]" },
	{ "TBS", "Tabasaran : Russia - Dagestan(0.1m)", "[tab]" },
	{ "TEL", "Telugu : India - Andhra Pr. (74m)", "[tel]" },
	{ "TEM", "Temme / Temne : Sierra Leone(1.5m)", "[tem]" },
	{ "TFT", "Tarifit : Morocco, northern(1.3m), Algeria", "[rif]" },
	{ "TGK", "Tangkhul / Tangkul Naga : India - Manipur,Nagaland(0.15m)", "[nmf]" },
	{ "TGR", "Tigre / Tigré : Eritrea(1m)", "[tig]" },
	{ "TGS", "Tangsa / Naga - Tase : Myanmar(60,000), India - Arunachal Pr. (40,000)", "[nst]" },
	{ "THA", "Tharu Buksa : India - Uttarakhand(43,000)", "[tkb]" },
	{ "TIG", "Tigrinya / Tigray : Ethiopia(4m), Eritrea(3m)", "[tir]" },
	{ "TJ", "Tajik : Tajikistan(3m), Uzbekistan(1m)", "[tgk]" },
	{ "TK", "Turkmen : Turkmenistan(3m), Iran(2m), Afghanistan(1.5m)", "[tuk]" },
	{ "TL", "Tai - Lu / Lu : China - Yunnan(0.3m), Myanmar(0.2m), Laos(0.1m)", "[khb]" },
	{ "TM", "Tamazight : Morocco, central(3m)", "[zgh]" },
	{ "TMG", "Tamang : Nepal(1.5m)", "[taj,tdg,tmk,tsf]" },
	{ "TMJ", "Tamajeq : Niger(0.8m), Mali(0.44m), Algeria(40,000)", "[taq,thv,thz,ttq]" },
	{ "TN", "Tai - Nua / Chinese Shan : China - Yunnan(0.5m), LAO / MYA / VTN(0.2m)", "[tdd]" },
	{ "TNG", "Tonga : Zambia(1m), Zimbabwe(0.1m)", "[toi]" },
	{ "TO", "Tongan : Tonga(0.1m)", "[ton]" },
	{ "TOK", "Tokelau : Tokelau(1000)", "[tkl]" },
	{ "TOR", "Torajanese / Toraja : Indonesia - Sulawesi(0.8m)", "[sda]" },
	{ "TP", "Tok Pisin : Papua New Guinea(4m)", "[tpi]" },
	{ "TS", "Tswana / SeTswana : Botswana(1m), South Africa(3m)", "[tsn]" },
	{ "TSA", "Tsangla", "[]" },
	{ "TSH", "Tshwa : Mocambique(1m)", "[tsc]" },
	{ "TT", "Tatar : Russia - Tatarstan,Bashkortostan(5m)", "[tat]" },
	{ "TTB", "Tatar - Bashkir service of Radio Liberty", "[]" },
	{ "TU", "Turkish : Turkey(46m), Bulgaria(0.6m), N Cyprus(0.2m)", "[tur]" } ,
	{ "TUL", "Tulu : India - Karnataka,Kerala(2m)", "[tcy]" },
	{ "TUM", "Tumbuka : Malawi(2m), Zambia(0.5m)", "[tum]" },
	{ "TUN", "Tunisian Arabic : Tunisia(9m)", "[aeb]" },
	{ "TV", "Tuva / Tuvinic : Russia - Tannu Tuva(0.25m)", "[tyv]" },
	{ "TW", "Taiwanese / Fujian / Hokkien / Min Nan(CHN 25m, TWN 15m, others 9m)", "[nan]" },
	{ "TWI", "Twi / Akan : Ghana(8m)", "[aka]" },
	{ "TWT", "Tachawit / Shawiya / Chaouia : Algeria(1.4m)", "[shy]" },
	{ "TZ", "Tamazight / Berber : Morocco(2m)", "[zgh,tzm]" },
	{ "UD", "Udmurt : Russia - Udmurtia(0.3m)", "[udm]" },
	{ "UI", "Uighur : China - Xinjiang(8m), Kazakhstan(0.3m)", "[uig]" },
	{ "UK", "Ukrainian : Ukraine(32m), Kazakhstan(0.9m), Moldova(0.6m)", "[ukr]" },
	{ "UM", "Umbundu : Angola(6m)", "[umb]" },
	{ "UR", "Urdu : Pakistan(104m), India(51m)", "[urd]" },
	{ "UZ", "Uzbek : Uzbekistan(16m)", "[uzn]" },
	{ "V", "Vasco / Basque / Euskera : Spain(0.6m), France(76,000)", "[eus]" },
	{ "VAD", "Vadari / Waddar / Od : India - Andhra Pr. (0.2m)", "[wbq]" },
	{ "VAR", "Varli / Warli : India - Maharashtra(0.6m)", "[vav]" },
	{ "Ves", "Vespers(Vaticane Radio)", "[]" },
	{ "Vn", "Vernacular = local language(s)", "[]" },
	{ "VN", "Vietnamese : Vietnam(66m)", "[vie]" },
	{ "VV", "Vasavi : India - Maharashtra,Gujarat(1m)", "[vas]" },
	{ "VX", "Vlax Romani / Romanes / Gypsy : Romania(0.2m), Russia(0.1m)", "[rmy]" },
	{ "W", "Wolof : Senegal(4m)", "[wol]" },
	{ "WA", "Wa / Parauk : South China(0.4m), Myanmar(0.4m)", "[prk]" },
	{ "WAO", "Waodani / Waorani : Ecuador(2000)", "[auc]" },
	{ "WE", "Wenzhou : dialect of WU", "[]" },
	{ "WT", "White Tai / Tai Don : Vietnam(0.3m), Laos(0.2m)", "[twh]" },
	{ "WU", "Wu : China - Jiangsu,Zhejiang(80m)", "[wuu]" },
	{ "XH", "Xhosa : South Africa(8m)", "[xho]" },
	{ "YAO", "Yao / Yawo : Malawi(2m), Mocambique(0.5m), Tanzania(0.4m)", "[yao]" },
	{ "YER", "Yerukula : India - Andhra Pr. (70,000)", "[yeu]" },
	{ "YI", "Yi / Nosu : China - Sichuan(2m)", "[iii]" },
	{ "YK", "Yakutian / Sakha : Russia - Sakha(0.5m)", "[sah]" },
	{ "YO", "Yoruba : Nigeria(20m), Benin(0.5m)", "[yor]" },
	{ "YOL", "Yolngu / Yuulngu : Australia - Northern Territory(4000)", "[djr]" },
	{ "YUN", "Dialects / languages of Yunnan(China)", "[]" },
	{ "YZ", "Yezidi program(Kurdish - Kurmanji language)", "[]" },
	{ "Z", "Zulu : South Africa(10m), Lesotho(0.3m)", "[zul]" },
	{ "ZA", "Zarma / Zama : Niger(2m)", "[dje]" },
	{ "ZD", "Zande : DR Congo(0.7m), South Sudan(0.35m)", "[zne]" },
	{ "ZG", "Zaghawa : Chad(87,000), Sudan(75,000)", "[zag]" },
	{ "ZH", "Zhuang : Southern China, 16 varieties(15m)", "[zha]" },
	{"ZWE", "Languages of Zimbabwe", "[]" }
};

static int n_gains = 60;
static int last_gain;
static int *gains;


#define NUM_BANDS	8            //  0				1			2			3			4			5	        6			7       
const double band_fmin[NUM_BANDS] = { 0.01,			12.0, 		30.0,		60.0,		120.0,		250.0,		420.0,		1000.0	};
const double band_fmax[NUM_BANDS] = { 11.999999,	29.999999,  59.999999,	119.999999,	249.999999,	419.999999,	999.999999, 2000.0	};
const int band_LNAgain[NUM_BANDS] = { 24,			24,			24,			24,			24,			24,			 7,			 5		};
const int band_MIXgain[NUM_BANDS] = { 19,			19,			19,			19,			19,			19,			19,			19		};
const int band_fullTune[NUM_BANDS]= {  1,			 1,			 1,			 1,			 1,			 1,			 1,			 1		};
const int band_MinGR[NUM_BANDS] =   {  0,			 0,			 0,		  	 0,			 0,			 0,			12,			14		}; // LNA OFF (LNA ON = 19)
const int band_MaxGR[NUM_BANDS] =   { 78,			78,			78,			78,			78,			78,			78,			78		}; // new gain map


static HMODULE ApiDll = NULL;
HMODULE Dll = NULL;

mir_sdr_Init_t								mir_sdr_Init_fn = NULL;
mir_sdr_Uninit_t							mir_sdr_Uninit_fn = NULL;
mir_sdr_ReadPacket_t						mir_sdr_ReadPacket_fn = NULL;
mir_sdr_SetRf_t								mir_sdr_SetRf_fn = NULL;
mir_sdr_SetFs_t								mir_sdr_SetFs_fn = NULL;
mir_sdr_SetGr_t								mir_sdr_SetGr_fn = NULL;
mir_sdr_SetGrParams_t						mir_sdr_SetGrParams_fn = NULL;
mir_sdr_SetDcMode_t							mir_sdr_SetDcMode_fn = NULL;
mir_sdr_SetDcTrackTime_t					mir_sdr_SetDcTrackTime_fn = NULL;
mir_sdr_SetSyncUpdateSampleNum_t			mir_sdr_SetSyncUpdateSampleNum_fn = NULL;
mir_sdr_SetSyncUpdatePeriod_t				mir_sdr_SetSyncUpdatePeriod_fn = NULL;
mir_sdr_ApiVersion_t						mir_sdr_ApiVersion_fn = NULL;
mir_sdr_ResetUpdateFlags_t					mir_sdr_ResetUpdateFlags_fn = NULL;
mir_sdr_DownConvert_t						mir_sdr_DownConvert_fn = NULL;
mir_sdr_SetParam_t							mir_sdr_SetParam_fn = NULL;
mir_sdr_SetPpm_t							mir_sdr_SetPpm_fn = NULL;
mir_sdr_SetLoMode_t							mir_sdr_SetLoMode_fn = NULL;
mir_sdr_SetGrAltMode_t						mir_sdr_SetGrAltMode_fn = NULL;
mir_sdr_DCoffsetIQimbalanceControl_t		mir_sdr_DCoffsetIQimbalanceControl_fn = NULL;
mir_sdr_DecimateControl_t					mir_sdr_DecimateControl_fn = NULL;
mir_sdr_AgcControl_t						mir_sdr_AgcControl_fn = NULL;
mir_sdr_StreamInit_t						mir_sdr_StreamInit_fn = NULL;
mir_sdr_StreamUninit_t						mir_sdr_StreamUninit_fn = NULL;
mir_sdr_Reinit_t						    mir_sdr_Reinit_fn = NULL;
mir_sdr_GetGrByFreq_t					    mir_sdr_GetGrByFreq_fn = NULL;
mir_sdr_DebugEnable_t						mir_sdr_DebugEnable_fn = NULL;
mir_sdr_SetTransferMode_t					mir_sdr_SetTransferMode_fn = NULL;
mir_sdr_GetDevices_t						mir_sdr_GetDevices_fn = NULL;
mir_sdr_SetDeviceIdx_t						mir_sdr_SetDeviceIdx_fn = NULL;
mir_sdr_GetHwVersion_t						mir_sdr_GetHwVersion_fn = NULL;
mir_sdr_ReleaseDeviceIdx_t					mir_sdr_ReleaseDeviceIdx_fn = NULL;
mir_sdr_GainChangeCallbackMessageReceived_t	mir_sdr_GainChangeCallbackMessageReceived_fn = NULL;

static int buffer_sizes[] = //in kBytes
{ 
	1,		2,		4,		8,
  16,   32,   64,  128,
 256,	 512,	1024
};

static int buffer_default=6;// 64kBytes

short CallbackBuffer[1024 * 1024]; // This just allocates the max buffer size ever required so we don't need to worry about resizing it - buffer_len_samples handles the amount used
short IQBuffer[504 * 2 * 64];           // Maximum samples per packet is 504 and we can have upto 64 packets per callback in multithreaded mode
int BufferCounter = 0;

typedef struct
{
	char vendor[256], product[256], serial[256];
} device;

static device *connected_devices = NULL;
static int device_count = 0;

// Thread handle
HANDLE worker_handle=INVALID_HANDLE_VALUE;
int FindSampleRateIdx(double);
void SaveSettings(void);
void LoadSettings(void);
void ProgramFreq(double freq, int abs);
void ProgramGr(int *gr, int *sysgr, int lnaen, int abs);
void ReinitAll(void);

/* ExtIO Callback */
void (* WinradCallBack)(int, int, float, void *) = NULL;
#define WINRAD_SRCHANGE     100
#define WINRAD_LOCHANGE     101
#define WINRAD_LOBLOCKED    102
#define WINRAD_LORELEASED   103
#define WINRAD_TUNECHANGED  105
#define WINRAD_DEMODCHANGED 106

int NUM_OF_HOTKEYS = 0;

static INT_PTR CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
HWND h_dialog = NULL;

static INT_PTR CALLBACK AdvancedDlgProc(HWND, UINT, WPARAM, LPARAM);
HWND h_AdvancedDialog = NULL;

static INT_PTR CALLBACK StationDlgProc(HWND, UINT, WPARAM, LPARAM);
HWND h_StationDialog = NULL;

static INT_PTR CALLBACK ProfilesDlgProc(HWND, UINT, WPARAM, LPARAM);
HWND h_ProfilesDialog = NULL;

static INT_PTR CALLBACK HelpDlgProc(HWND, UINT, WPARAM, LPARAM);
HWND h_HelpDialog = NULL;

static INT_PTR CALLBACK StationConfigDlgProc(HWND, UINT, WPARAM, LPARAM);
HWND h_StationConfigDialog = NULL;

HWND h_SplashScreen = NULL;

int pll_locked=0;
char FreqoffsetTxt[256];
char GainReductionTxt[256];
char AGCsetpointTxt[256];
char FilenameTxt[256];

typedef LONG NTSTATUS;

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif

std::wstring GetKeyPathFromKKEY(HKEY key)
{
	std::wstring keyPath;
	if (key != NULL)
	{
		HMODULE dll = LoadLibrary("ntdll.dll");
		if (dll != NULL) {
			typedef DWORD(__stdcall *NtQueryKeyType)(
				HANDLE  KeyHandle,
				int KeyInformationClass,
				PVOID  KeyInformation,
				ULONG  Length,
				PULONG  ResultLength);

			NtQueryKeyType func = reinterpret_cast<NtQueryKeyType>(::GetProcAddress(dll, "NtQueryKey"));

			if (func != NULL) {
				DWORD size = 0;
				DWORD result = 0;
				result = func(key, 3, 0, 0, &size);
				if (result == STATUS_BUFFER_TOO_SMALL)
				{
					size = size + 2;
					wchar_t* buffer = new (std::nothrow) wchar_t[size / sizeof(wchar_t)]; // size is in bytes
					if (buffer != NULL)
					{
						result = func(key, 3, buffer, size, &size);
						if (result == STATUS_SUCCESS)
						{
							buffer[size / sizeof(wchar_t)] = L'\0';
							keyPath = std::wstring(buffer + 2);
						}

						delete[] buffer;
					}
				}
			}

			FreeLibrary(dll);
		}
	}
	return keyPath;
}

const char * getMirErrText(mir_sdr_ErrT err)
{
	switch (err)
	{
	case mir_sdr_Success:				return 0;
	case mir_sdr_Fail:					return "Fail";
	case mir_sdr_InvalidParam:			return "Invalid Parameters";
	case mir_sdr_OutOfRange:			return "Out of Range";
	case mir_sdr_GainUpdateError:		return "Gain Update Error";
	case mir_sdr_RfUpdateError:			return "Rf Update Error";
	case mir_sdr_FsUpdateError:			return "Fs Update Error";
	case mir_sdr_HwError:				return "Hw Error";
	case mir_sdr_AliasingError:			return "Aliasing Error";
	case mir_sdr_AlreadyInitialised:	return "Already Initialized";
	case mir_sdr_NotInitialised:		return "Not Initialized";
	default:							return "Unknown";
	}
}

void string_realloc_and_copy(char **dest, const char *src)
{
	*dest = (char *)realloc(*dest, strlen(src) + 1);
	strcpy_s(*dest, strlen(src) + 1, src);
}

struct station* station_new()
{
	station *s = (station *)malloc(sizeof(station));
	s->days = NULL;
	s->freq = NULL;
	s->itu = NULL;
	s->language = NULL;
	s->name = NULL;
	s->tac = NULL;
	s->time = NULL;
	s->next = NULL;

	return s;
}

void station_free(station *s)
{
	::free(s->days);
	::free(s->freq);
	::free(s->itu);
	::free(s->language);
	::free(s->name);
	::free(s->tac);
	::free(s->time);
	::free(s->minTime);
	::free(s->maxTime);
	::free(s->next);
	::free(s);
}

void station_set_freq(station *s, const char *freq)
{
	string_realloc_and_copy(&s->freq, freq);
}

void station_set_name(station *s, const char *name)
{
	string_realloc_and_copy(&s->name, name);
}

void station_set_itu(station *s, const char *itu)
{
	string_realloc_and_copy(&s->itu, itu);
}

void station_set_tac(station *s, const char *tac)
{
	string_realloc_and_copy(&s->tac, tac);
}

void station_set_time(station *s, const char *time)
{
	string_realloc_and_copy(&s->time, time);
}

void station_set_days(station *s, const char *days)
{
	string_realloc_and_copy(&s->days, days);
}

void station_set_MinMaxTimes(station *s, const char *totalTime)
{
	s->minTime[0] = totalTime[0];
	s->minTime[1] = totalTime[1];
	s->minTime[2] = totalTime[2];
	s->minTime[3] = totalTime[3];
	s->minTime[4] = '\0';
	s->maxTime[0] = totalTime[5];
	s->maxTime[1] = totalTime[6];
	s->maxTime[2] = totalTime[7];
	s->maxTime[3] = totalTime[8];
	s->maxTime[4] = '\0';
}

#if 0
void copyCSVtoHDD(char *filename)
{
	char sendMessage[256];
	sprintf_s(sendMessage, 254, "GET /dx/sked-b17.csv HTTP/1.1\r\nHost: www.eibispace.de\r\n\r\n");
	WSADATA wsaData;
	char *hostname = "www.eibispace.de";
	char ip[100];
	struct hostent *he;
	struct in_addr **addr_list;
	int i, iResult;
	SOCKET s;
	struct sockaddr_in server;
	char recvBuf[BUFFER_LEN] = { 0 };
	HANDLE fhandle;
	string response;
	int iResponseLength = 0;
	unsigned int offset;
	const char lb[] = "\r\n\r\n";
	string res2;
	DWORD dw;

#ifdef DEBUG_ENABLE
	OutputDebugString("copyCSVtoHDD");
	OutputDebugString(filename);
#endif

	fhandle = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	he = gethostbyname(hostname);

	if (he == NULL)
	{
		localDBExists = false;
		if (h_StationConfigDialog != NULL)
			Edit_SetText(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_STATUS), "Database Status: No route to host");
		return;
	}

	addr_list = (struct in_addr **) he->h_addr_list;
	for (i = 0; addr_list[i] != NULL; i++)
	{
		strcpy_s(ip, inet_ntoa(*addr_list[i]));
	}
	s = socket(AF_INET, SOCK_STREAM, 0);
	server.sin_addr.s_addr = inet_addr(ip);
	server.sin_family = AF_INET;
	server.sin_port = htons(80);
	connect(s, (struct sockaddr *)&server, sizeof(server));
	send(s, sendMessage, strlen(sendMessage), 0);
	shutdown(s, SD_SEND);

	while ((iResult = recv(s, recvBuf, BUFFER_LEN - 1, 0)) > 0)
	{
		response.append(recvBuf, iResult);
		iResponseLength += iResult;
		ZeroMemory(recvBuf, BUFFER_LEN);
	}

	offset = response.find(lb) + 4;
	if (offset != string::npos)
	{
		res2.assign(response, offset, response.size());
		WriteFile(fhandle, res2.data(), res2.size(), &dw, 0);
	}

	localDBExists = true;
	if (h_StationConfigDialog != NULL)
		Edit_SetText(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_STATUS), "Database Status: Good");

	closesocket(s);
	WSACleanup();
	CloseHandle(fhandle);
}
#endif

bool copyCSVtoHDD(char *filename)
{
	char buffer[BUFFER_LEN];
	struct sockaddr_in serveraddr;
	int sock;

	WSADATA wsaData;
	USHORT port = 80;
	string shost = "www.eibispace.de";
	string request = "GET /dx/sked-b17.csv HTTP/1.1\r\nHost: www.eibispace.de\r\n\r\n";


	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("WSAStartup error");
#endif
		return false;
	}

	if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("socket error");
#endif
		return false;
	}

	memset(&serveraddr, 0, sizeof(serveraddr));

	hostent *record = gethostbyname(shost.c_str());

#ifdef DEBUG_ENABLE
	char tmpString[1024];
	sprintf_s(tmpString, 1024, "%s", record->h_name);
	OutputDebugString(tmpString);
#endif

	in_addr *address = (in_addr *)record->h_addr;
	string ipd = inet_ntoa(*address);
	const char *ipaddr = ipd.c_str();

	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(ipaddr);
	serveraddr.sin_port = htons(port);

	if (connect(sock, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("can't connect");
#endif
		localDBExists = false;
		if (h_StationConfigDialog != NULL)
			Edit_SetText(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_STATUS), "Database Status: No route to host");
		return false;
	}

	if (send(sock, request.c_str(), request.length(), 0) == INVALID_SOCKET)
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("can't send");
#endif
		return false;
	}
//	shutdown(sock, SD_SEND);

	int nRecv, npos;
	nRecv = recv(sock, (char *)&buffer, BUFFER_LEN, 0);

	if (nRecv == 0)
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("received 0 bytes header");
#endif
		return false;
	}

	if (nRecv == SOCKET_ERROR)
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("nRecv initial socket error");
#endif
		return false;
	}

	string str_buff = buffer;
	npos = str_buff.find("\r\n\r\n");

	HANDLE hFile;
	hFile = CreateFileA(filename, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (hFile == INVALID_HANDLE_VALUE)
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("cannot create file");
#endif
		return false;
	}
	if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER)
	{
		DWORD ss;

		while ((nRecv > 0) && (nRecv != INVALID_SOCKET))
		{
			if (npos > 0)
			{
				char bf[BUFFER_LEN];
				memcpy(bf, buffer + (npos + 4), nRecv - (npos + 4));
				if (WriteFile(hFile, bf, nRecv - (npos + 4), &ss, NULL) == 0)
				{
#ifdef DEBUG_ENABLE
					OutputDebugString("initial write failed");
#endif
					return false;
				}
			}
			else
			{
				if (WriteFile(hFile, buffer, nRecv, &ss, NULL) == 0)
				{
#ifdef DEBUG_ENABLE
					OutputDebugString("main write failed");
#endif
					return false;
				}
			}

			ZeroMemory(&buffer, sizeof(buffer));
			nRecv = recv(sock, (char *)&buffer, BUFFER_LEN, 0);
			if (nRecv == SOCKET_ERROR)
			{
#ifdef DEBUG_ENABLE
				OutputDebugString("nRecv main socket error");
#endif
				return false;
			}
			str_buff = buffer;
			npos = str_buff.find("\r\n\r\n");
		}

		if (nRecv == 0)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("nRecv == 0");
#endif
		}

		if (nRecv == INVALID_SOCKET)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("nRecv final invalid socket");
#endif
			return false;
		}

		localDBExists = true;
		if (h_StationConfigDialog != NULL)
			Edit_SetText(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_STATUS), "Database Status: Good");

		CloseHandle(hFile);
		closesocket(sock);
		WSACleanup();
		return true;
	}
	else
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("file pointer error");
#endif
		CloseHandle(hFile);
		closesocket(sock);
		WSACleanup();
		return false;
	}
}

#pragma comment(lib, "Shell32.lib") // Windows Shell Library

bool DirectoryExists(LPCWSTR szPath)
{
	DWORD dwAttrib = GetFileAttributesW(szPath);

	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool FileExists(const char *filename)
{
	std::ifstream infile(filename);
	return infile.good();
}

bool FileModifiedToday(char *pFileName)
{
	struct stat attrib;
	time_t rawtime;
	tm timeinfo;
	tm timeinfoNow;

	::time(&rawtime);
	stat(pFileName, &attrib);

	gmtime_s(&timeinfo, &(attrib.st_mtime));
	gmtime_s(&timeinfoNow, &rawtime);

#ifdef DEBUG_ENABLE
	char tmpString[1204];
	sprintf_s(tmpString, 1024, "%d %d %d", (1900 + timeinfo.tm_year), (1 + timeinfo.tm_mon), timeinfo.tm_mday);
	OutputDebugString(tmpString);
	sprintf_s(tmpString, 1024, "%d %d %d", (1900 + timeinfoNow.tm_year), (1 + timeinfoNow.tm_mon), timeinfoNow.tm_mday);
	OutputDebugString(tmpString);
#endif

	if ((timeinfo.tm_year == timeinfoNow.tm_year) && (timeinfo.tm_mon == timeinfoNow.tm_mon) && (timeinfo.tm_mday == timeinfoNow.tm_mday))
	{
		return true;
	}
	return false;
}

void startupPopUpWindow(void)
{
	h_SplashScreen = CreateWindowEx(WS_EX_CLIENTEDGE, "STATIC", "", WS_POPUP | WS_DLGFRAME,
		GetSystemMetrics(SM_CXSCREEN)/2-150, GetSystemMetrics(SM_CYSCREEN)/2-40, 300, 80, NULL, NULL, NULL, NULL);
	
	SetBkMode((HDC)h_SplashScreen, TRANSPARENT);
	HWND label = CreateWindow("static", "label", WS_CHILD | WS_VISIBLE, 20, 20, 260, 20, h_SplashScreen, NULL, NULL, NULL);
	SetWindowText(label, "SDRplay ExtIO loading, please wait...");
	SetBkMode((HDC)label, TRANSPARENT);
	ShowWindow(h_SplashScreen, SW_SHOWNORMAL);
	UpdateWindow(h_SplashScreen);
}

void closePopUpWindow(void)
{
	if (h_SplashScreen != NULL)
	{
		ShowWindow(h_SplashScreen, SW_HIDE);
		DestroyWindow(h_SplashScreen);
	}
}

extern "C"
bool  LIBSDRplay_API __stdcall InitHW(char *name, char *model, int &type)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("InitHW");
#endif
		char APIkeyValue[8192];
		char tmpStringA[8192];
		char str[1024 + 8192];
		DWORD APIkeyValue_length = 8192;
		char APIver[32];
		DWORD APIver_length = 32;
		HKEY APIkey;
		HKEY Settingskey;
		mir_sdr_ErrT err;
		int error, tempRB, tempRB2;
		DWORD IntSz = sizeof(int);
		WCHAR csvPath[8192];

		gains = new int[n_gains];
		for (int i = 0; i < 60; i++)
		{
			gains[i] = i + 19;
		}
		
		if (RegOpenKey(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\SDRplay\\API"), &APIkey) != ERROR_SUCCESS)
		{
			error = GetLastError();
			_stprintf_s(str, TEXT("Failed to locate API registry entry\nHKEY_LOCAL_MACHINE\\SOFTWARE\\SDRplay\\API\nERROR %d\n"), error);
			MessageBox(NULL, str ,TEXT("SDRplay ExtIO DLL"),MB_ICONERROR | MB_OK);
			return false;
		}
		else
		{
			RegQueryValueEx(APIkey, "Install_Dir", NULL, NULL, (LPBYTE)&APIkeyValue, &APIkeyValue_length);
			RegQueryValueEx(APIkey, "Version", NULL, NULL, (LPBYTE)&APIver, &APIver_length);
			sprintf_s(apiVersion, sizeof(apiVersion), APIver);
			wcscpy_s(regPath, GetKeyPathFromKKEY(APIkey).c_str());
			RegCloseKey(APIkey);
		}

		#ifndef _WIN64
			sprintf_s(tmpStringA, 8192, "%s\\x86\\mir_sdr_api.dll", APIkeyValue);
		#else
			sprintf_s(tmpStringA, 8192, "%s\\x64\\mir_sdr_api.dll", APIkeyValue);
		#endif

		sprintf_s(apiPath, sizeof(apiPath), "%s", tmpStringA);

		LPCSTR ApiDllName = (LPCSTR)tmpStringA;

		if (ApiDll == NULL)
		{		
			ApiDll = LoadLibrary(ApiDllName);
		}
		if (ApiDll == NULL)
		{
			ApiDll = LoadLibrary("mir_sdr_api.dll");
		}
		if (ApiDll == NULL)
		{
			error = GetLastError();
			_stprintf_s(str, TEXT("Failed to load API DLL\n%s\nERROR %d\n"), ApiDllName, error);
			MessageBox(NULL, str, TEXT("SDRplay ExtIO DLL"), MB_ICONERROR | MB_OK);
			return false;
		}

		mir_sdr_Init_fn = (mir_sdr_Init_t)GetProcAddress(ApiDll, "mir_sdr_Init");
		mir_sdr_Uninit_fn = (mir_sdr_Uninit_t)GetProcAddress(ApiDll, "mir_sdr_Uninit");
		mir_sdr_ReadPacket_fn = (mir_sdr_ReadPacket_t)GetProcAddress(ApiDll, "mir_sdr_ReadPacket");
		mir_sdr_SetRf_fn = (mir_sdr_SetRf_t)GetProcAddress(ApiDll, "mir_sdr_SetRf");
		mir_sdr_SetFs_fn = (mir_sdr_SetFs_t)GetProcAddress(ApiDll, "mir_sdr_SetFs");
		mir_sdr_SetGr_fn = (mir_sdr_SetGr_t)GetProcAddress(ApiDll, "mir_sdr_SetGr");
		mir_sdr_SetGrParams_fn = (mir_sdr_SetGrParams_t)GetProcAddress(ApiDll, "mir_sdr_SetGrParams");
		mir_sdr_SetDcMode_fn = (mir_sdr_SetDcMode_t)GetProcAddress(ApiDll, "mir_sdr_SetDcMode");
		mir_sdr_SetDcTrackTime_fn = (mir_sdr_SetDcTrackTime_t)GetProcAddress(ApiDll, "mir_sdr_SetDcTrackTime");
		mir_sdr_SetSyncUpdateSampleNum_fn = (mir_sdr_SetSyncUpdateSampleNum_t)GetProcAddress(ApiDll, "mir_sdr_SetSyncUpdateSampleNum");
		mir_sdr_SetSyncUpdatePeriod_fn = (mir_sdr_SetSyncUpdatePeriod_t)GetProcAddress(ApiDll, "mir_sdr_SetSyncUpdatePeriod");
		mir_sdr_ApiVersion_fn = (mir_sdr_ApiVersion_t)GetProcAddress(ApiDll, "mir_sdr_ApiVersion");
		mir_sdr_ResetUpdateFlags_fn = (mir_sdr_ResetUpdateFlags_t)GetProcAddress(ApiDll, "mir_sdr_ResetUpdateFlags");
		mir_sdr_DownConvert_fn = (mir_sdr_DownConvert_t)GetProcAddress(ApiDll, "mir_sdr_DownConvert");
		mir_sdr_SetParam_fn = (mir_sdr_SetParam_t)GetProcAddress(ApiDll, "mir_sdr_SetParam");
		mir_sdr_SetPpm_fn = (mir_sdr_SetPpm_t)GetProcAddress(ApiDll, "mir_sdr_SetPpm");
		mir_sdr_SetLoMode_fn = (mir_sdr_SetLoMode_t)GetProcAddress(ApiDll, "mir_sdr_SetLoMode");
		mir_sdr_SetGrAltMode_fn = (mir_sdr_SetGrAltMode_t)GetProcAddress(ApiDll, "mir_sdr_SetGrAltMode");
		mir_sdr_DCoffsetIQimbalanceControl_fn = (mir_sdr_DCoffsetIQimbalanceControl_t)GetProcAddress(ApiDll, "mir_sdr_DCoffsetIQimbalanceControl");
		mir_sdr_DecimateControl_fn = (mir_sdr_DecimateControl_t)GetProcAddress(ApiDll, "mir_sdr_DecimateControl");
		mir_sdr_AgcControl_fn = (mir_sdr_AgcControl_t)GetProcAddress(ApiDll, "mir_sdr_AgcControl");
		mir_sdr_StreamInit_fn = (mir_sdr_StreamInit_t)GetProcAddress(ApiDll, "mir_sdr_StreamInit");
		mir_sdr_StreamUninit_fn = (mir_sdr_StreamUninit_t)GetProcAddress(ApiDll, "mir_sdr_StreamUninit");
		mir_sdr_Reinit_fn = (mir_sdr_Reinit_t)GetProcAddress(ApiDll, "mir_sdr_Reinit");
		mir_sdr_GetGrByFreq_fn = (mir_sdr_GetGrByFreq_t)GetProcAddress(ApiDll, "mir_sdr_GetGrByFreq");
		mir_sdr_DebugEnable_fn = (mir_sdr_DebugEnable_t)GetProcAddress(ApiDll, "mir_sdr_DebugEnable");
		mir_sdr_SetTransferMode_fn = (mir_sdr_SetTransferMode_t)GetProcAddress(ApiDll, "mir_sdr_SetTransferMode");
		mir_sdr_GetDevices_fn = (mir_sdr_GetDevices_t)GetProcAddress(ApiDll, "mir_sdr_GetDevices");
		mir_sdr_SetDeviceIdx_fn = (mir_sdr_SetDeviceIdx_t)GetProcAddress(ApiDll, "mir_sdr_SetDeviceIdx");
		mir_sdr_GetHwVersion_fn = (mir_sdr_GetHwVersion_t)GetProcAddress(ApiDll, "mir_sdr_GetHwVersion");
		mir_sdr_ReleaseDeviceIdx_fn = (mir_sdr_ReleaseDeviceIdx_t)GetProcAddress(ApiDll, "mir_sdr_ReleaseDeviceIdx");
		mir_sdr_GainChangeCallbackMessageReceived_fn = (mir_sdr_GainChangeCallbackMessageReceived_t)GetProcAddress(ApiDll, "mir_sdr_GainChangeCallbackMessageReceived");
		
		if	(   (mir_sdr_Init_fn == NULL)
			 || (mir_sdr_Uninit_fn == NULL)
			 || (mir_sdr_ReadPacket_fn == NULL)
			 || (mir_sdr_SetRf_fn == NULL)
			 || (mir_sdr_SetFs_fn == NULL)
			 || (mir_sdr_SetGr_fn == NULL)
			 || (mir_sdr_SetGrParams_fn == NULL)
			 || (mir_sdr_SetDcMode_fn == NULL)
			 || (mir_sdr_SetDcTrackTime_fn == NULL)
			 || (mir_sdr_SetSyncUpdateSampleNum_fn == NULL)
			 || (mir_sdr_SetSyncUpdatePeriod_fn == NULL)
			 || (mir_sdr_ApiVersion_fn == NULL)
			 || (mir_sdr_ResetUpdateFlags_fn == NULL)
			 || (mir_sdr_DownConvert_fn == NULL)
			 || (mir_sdr_SetParam_fn == NULL)
			 || (mir_sdr_SetPpm_fn == NULL)
			 || (mir_sdr_SetLoMode_fn == NULL)
			 || (mir_sdr_SetGrAltMode_fn == NULL)
			 || (mir_sdr_DCoffsetIQimbalanceControl_fn == NULL)
			 || (mir_sdr_DecimateControl_fn == NULL)
			 || (mir_sdr_AgcControl_fn == NULL)
			 || (mir_sdr_StreamInit_fn == NULL)
			 || (mir_sdr_StreamUninit_fn == NULL)
			 || (mir_sdr_Reinit_fn == NULL)
			 || (mir_sdr_DebugEnable_fn == NULL)
			 || (mir_sdr_SetTransferMode_fn == NULL)
			 || (mir_sdr_GetGrByFreq_fn == NULL)
			 || (mir_sdr_GetDevices_fn == NULL)
			 || (mir_sdr_SetDeviceIdx_fn == NULL)
			 || (mir_sdr_GetHwVersion_fn == NULL)
			 || (mir_sdr_ReleaseDeviceIdx_fn == NULL)
			 || (mir_sdr_GainChangeCallbackMessageReceived_fn == NULL)
			)
		{
			MessageBox(NULL, TEXT("Failed to map API DLL functions\n"), TEXT("SDRplay ExtIO DLL"), MB_ICONERROR | MB_OK);
			FreeLibrary(ApiDll);
			return false;
		}

#ifdef DEBUG_ENABLE
		OutputDebugString("Get Device Information...");
		mir_sdr_DebugEnable_fn(RSP_DEBUG);
#endif
		bool foundRSP1 = false;
		mir_sdr_GetDevices_fn(&devices[0], &numDevs, MAXNUMOFDEVS);
		if (devices != NULL)
		{
			unsigned int i;
			for (i = 0; i < numDevs; i++)
			{
				_snprintf_s(msgbuf, 1024, "[%d/%d] DevNm=%s SerNo=%s hwVer=%d, devAvail=%d", (i + 1), numDevs, devices[i].DevNm, devices[i].SerNo, devices[i].hwVer, devices[i].devAvail);
				OutputDebugString(msgbuf);
				if (devices[i].hwVer == 1 && devices[i].devAvail == 1)
				{
					mir_sdr_SetDeviceIdx_fn(i);
					foundRSP1 = true;
					break;
				}
			}
			if (!foundRSP1) {
				OutputDebugString("No RSP1s found.");
				MessageBox(NULL, TEXT("Failed to find any available RSP1s"), TEXT("SDRplay ExtIO Error"), MB_ICONERROR | MB_OK);
				return false;
			}
		}
		else
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("No RSPs found.");
			MessageBox(NULL, TEXT("Failed to find any RSPs"), TEXT("SDRplay ExtIO Error"), MB_ICONERROR | MB_OK);
			return false;
#endif
		}

		// The code belkow performs an init to check for the presence of the hardware.  An uninit is done afterwards.

		IFMode = mir_sdr_IF_Zero;
		Bandwidth = mir_sdr_BW_1_536;
		lastFreqBand = -1;

		err = mir_sdr_Init_fn(60, 2.0, 98.8, Bandwidth, IFMode, &SampleCount);
		if (err == mir_sdr_Success)
		{
			error = RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\SDRplay\\Settings"), 0, KEY_ALL_ACCESS, &Settingskey);
			if (error == ERROR_SUCCESS)
			{
				error = RegQueryValueEx(Settingskey, "StationLookup", NULL, NULL, (LPBYTE)&tempRB, &IntSz);
				if (error != ERROR_SUCCESS)
				{
#ifdef DEBUG_ENABLE
					OutputDebugString("Failed to recall saved StationLookup state");
#endif
					stationLookup = false;
				}
				if (tempRB == 1)
					stationLookup = true;
				else
					stationLookup = false;

				error = RegQueryValueEx(Settingskey, "WorkOffline", NULL, NULL, (LPBYTE)&tempRB2, &IntSz);
				if (error != ERROR_SUCCESS)
				{
#ifdef DEBUG_ENABLE
					OutputDebugString("Failed to recall saved WorkOffline state");
#endif
					workOffline = false;
				}
				if (tempRB2 == 1)
					workOffline = true;
				else
					workOffline = false;


				if (stationLookup)
				{
					if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, csvPath)))
					{
						PathAppendW(csvPath, L"\\SDRplay");

						LPCWSTR csvPathW = csvPath;
						if (!DirectoryExists(csvPathW))
							SHCreateDirectory(0, csvPath);

						PathAppendW(csvPath, L"\\sked-b17.csv");
						char *newString = new char[wcslen(csvPath) + 1];
						size_t iSize;
						wcstombs_s(&iSize, newString, 8192, csvPath, wcslen(csvPath));

						if (FileExists(newString))
						{
#ifdef DEBUG_ENABLE
							OutputDebugString("csv file exists");
#endif
							localDBExists = true;
							if (!FileModifiedToday(newString) && !workOffline)
							{
#ifdef DEBUG_ENABLE
								OutputDebugString("old file, creating new");
#endif
								startupPopUpWindow();
								if (!copyCSVtoHDD(newString))
								{
#ifdef DEBUG_ENABLE
									OutputDebugString("error during copy");
#endif
									localDBExists = false;
									if (h_StationConfigDialog != NULL)
										Edit_SetText(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_STATUS), "Database Status: Copy failed");
								}
								else
								{
									localDBExists = true;
									if (h_StationConfigDialog != NULL)
										Edit_SetText(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_STATUS), "Database Status: Good");
								}
							}
						}
						else
						{
#ifdef DEBUG_ENABLE
							OutputDebugString("no file exists");
#endif
							if (workOffline)
							{
								localDBExists = false;
								if (h_StationConfigDialog != NULL)
									Edit_SetText(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_STATUS), "Database Status: No database");
							}
							else
							{
								startupPopUpWindow();
								if (!copyCSVtoHDD(newString))
								{
#ifdef DEBUG_ENABLE
									OutputDebugString("error during copy");
#endif
									localDBExists = false;
									if (h_StationConfigDialog != NULL)
										Edit_SetText(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_STATUS), "Database Status: Copy failed");
								}
								else
								{
									localDBExists = true;
									if (h_StationConfigDialog != NULL)
										Edit_SetText(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_STATUS), "Database Status: Good");
								}
							}
						}
					}
					else
						MessageBox(NULL, "Error locating local temporary storage", NULL, MB_OK);
				}
			}

#ifdef DEBUG_ENABLE
			OutputDebugString("Hardware sucessfully initalised & Registry paths found");
#endif
			sprintf_s(name, 32, "%s", "SDRplay RSP");
			sprintf_s(model, 32, "%s", "Radio Spectrum Processor");
			type = 3;	
			mir_sdr_Uninit_fn();
			closePopUpWindow();
			return true;
		}
		else
		{
			closePopUpWindow();
			MessageBox(NULL, TEXT("Warning SDRplay Hardware not found"), TEXT("SDRplay ExtIO DLL"), MB_ICONERROR | MB_OK);
			return false;
		}
}

extern "C"
bool  LIBSDRplay_API __stdcall OpenHW()
{
#ifdef DEBUG_ENABLE
	OutputDebugString("OpenHW");
#endif
    h_dialog=CreateDialog(hInst, MAKEINTRESOURCE(IDD_SDRPLAY_SETTINGS), NULL, (DLGPROC)MainDlgProc);
	ShowWindow(h_dialog, SW_HIDE);
	BringWindowToTop(h_dialog);
	return true;
}

extern "C"
long LIBSDRplay_API __stdcall SetHWLO(unsigned long freq)
{
	TCHAR str[255];
	int FreqBand = 0;

	#ifdef DEBUG_ENABLE
	OutputDebugString("SetHWLO");
	#endif
	FreqBand = 0;

	#ifdef DEBUG_ENABLE
	_snprintf_s(msgbuf, 1024, "%s %lu", "Frequency is", freq);
	OutputDebugString(msgbuf);
	#endif
	WinradCallBack(-1, WINRAD_LOBLOCKED, 0, NULL);
	if (freq > 2000000000)
	{
		MessageBox(NULL, "Warning Out of Range", TEXT("WARNING"), MB_OK | MB_SYSTEMMODAL | MB_TOPMOST | MB_ICONEXCLAMATION);
		WinradCallBack(-1, WINRAD_LOCHANGE, 0, NULL);
		WinradCallBack(-1, WINRAD_LORELEASED, 0, NULL);
		return 2000000000;
	}
	if (freq < ((UINT)LowerFqLimit * 1000))
	{
		MessageBox(NULL, "Warning Out of Range", TEXT("WARNING"), MB_OK | MB_SYSTEMMODAL | MB_TOPMOST | MB_ICONEXCLAMATION);
		WinradCallBack(-1, WINRAD_LOCHANGE, 0, NULL);
		WinradCallBack(-1, WINRAD_LORELEASED, 0, NULL);
		return -100000;
	}

	//Code to limit Dialog Box Movement
	Frequency = (double)freq / 1000000;

	if (!Running)
	{
      mir_sdr_GetGrByFreq_fn(Frequency, (mir_sdr_BandT *)&FreqBand, (int *)&GainReduction, LNAEnable, &SystemGainReduction, mir_sdr_USE_SET_GR_ALT_MODE);
   }
	else //if (Running)
	{
      mir_sdr_Reinit_fn((int *)&GainReduction, 0.0, Frequency, mir_sdr_BW_Undefined, mir_sdr_IF_Undefined, mir_sdr_LO_Undefined, (int)LNAEnable, &SystemGainReduction, mir_sdr_USE_SET_GR_ALT_MODE, pZERO,
                        (mir_sdr_ReasonForReinitT)(mir_sdr_CHANGE_GR | mir_sdr_CHANGE_RF_FREQ));
	}

   if (!LNAEnable)
   {
      //SendMessage(GetDlgItem(ghwndDlg, IDC_GR_S), UDM_SETRANGE, (WPARAM)true, (LPARAM)MAKELONG(band_MinGR[FreqBand], 78));
      SendMessage(GetDlgItem(ghwndDlg, IDC_GAINSLIDER), TBM_SETRANGEMIN, (WPARAM)true, (LPARAM)band_MinGR[FreqBand]);
      SendMessage(GetDlgItem(ghwndDlg, IDC_GAINSLIDER), TBM_SETRANGEMAX, (WPARAM)true, (LPARAM)78);
      sprintf_s(str, sizeof(str), "%s %d %s", "LNA GR", band_LNAgain[FreqBand], "dB");
      Edit_SetText(GetDlgItem(h_dialog, IDC_LNAGR), str);
      Edit_SetText(GetDlgItem(h_dialog, IDC_LNASTATE), "LNA OFF");
      sprintf_s(str, sizeof(str), "Total System Gain Reduction %d dB", SystemGainReduction);
      Edit_SetText(GetDlgItem(h_dialog, IDC_TOTALGR), str);
   }
   else
   {
      //SendMessage(GetDlgItem(ghwndDlg, IDC_GR_S), UDM_SETRANGE, (WPARAM)true, (LPARAM)MAKELONG(19, 78));
      SendMessage(GetDlgItem(ghwndDlg, IDC_GAINSLIDER), TBM_SETRANGEMIN, (WPARAM)true, (LPARAM)19);
      SendMessage(GetDlgItem(ghwndDlg, IDC_GAINSLIDER), TBM_SETRANGEMAX, (WPARAM)true, (LPARAM)78);
   }

   WinradCallBack(-1, WINRAD_LORELEASED, 0, NULL);
	return 0;
}

mir_sdr_LoModeT GetLoMode(void)
{
   if (LOplanAuto)
   {
      return mir_sdr_LO_Auto;
}
   else
   {
      switch (LOplan)
      {
      case LO120MHz: return mir_sdr_LO_120MHz;
      case LO144MHz: return mir_sdr_LO_144MHz;
      case LO168MHz: return mir_sdr_LO_168MHz;
      default:       return mir_sdr_LO_120MHz;
      }
   }
}

void ProgramFreq(double freq, int abs)
{
   int i;
   for (i = 0; i < 4; i++)
   {
      if (mir_sdr_SetRf_fn(freq, abs, 0) != mir_sdr_RfUpdateError)
      {
         break;
      }
      else
      {
         Sleep(25);
      }
   }
   if (i == 4)
   {
      mir_sdr_ResetUpdateFlags_fn(0, 1, 0);
      mir_sdr_SetRf_fn(freq, abs, 0);
   }
}

void ProgramGr(int *gr, int *sysgr, int lnaen, int abs)
{
   int i;
   for (i = 0; i < 4; i++)
   {
      if (mir_sdr_SetGrAltMode_fn(gr, lnaen, sysgr, abs, 0) != mir_sdr_GainUpdateError)
      {
         break;
      }
      else
      {
         Sleep(25);
      }
   }
   if (i == 4)
   {
      mir_sdr_ResetUpdateFlags_fn(1, 0, 0);
      mir_sdr_SetGrAltMode_fn(gr, lnaen, sysgr, abs, 0);
   }
}

void ReinitAll(void)
{
   mir_sdr_Reinit_fn((int *)&GainReduction, samplerates[SampleRateIdx].value, Frequency, Bandwidth, IFMode, LOMode, (int)LNAEnable, &SystemGainReduction, mir_sdr_USE_SET_GR_ALT_MODE, &SampleCount,
      (mir_sdr_ReasonForReinitT)(mir_sdr_CHANGE_GR | mir_sdr_CHANGE_FS_FREQ | mir_sdr_CHANGE_RF_FREQ | mir_sdr_CHANGE_BW_TYPE | mir_sdr_CHANGE_IF_TYPE | mir_sdr_CHANGE_LO_MODE));
   return;
}

void gaincallbackMirSdr(unsigned int gRdB, unsigned int lnaGRdB, void *)
{
	char str[512];

#ifdef RSP_DEBUG
	sprintf_s(str, sizeof(str), "gRdB: %d, lnaGrdB: %d", gRdB, lnaGRdB);
	OutputDebugString(str);
#endif

	if (gRdB < mir_sdr_GAIN_MESSAGE_START_ID)
	{
		GainReduction = gRdB;
		if (AGCEnabled != mir_sdr_AGC_DISABLE) {
			sprintf_s(str, sizeof(str), "%d", GainReduction);
			Edit_SetText(GetDlgItem(h_dialog, IDC_GR), str);
			SendMessage(GetDlgItem(h_dialog, IDC_GAINSLIDER), TBM_SETPOS, (WPARAM)true, (LPARAM)GainReduction);
		}
		sprintf_s(str, sizeof(str), "IF Gain Reduction %d dB", GainReduction);
		Edit_SetText(GetDlgItem(h_dialog, IDC_IFGR), str);

		LnaGr = lnaGRdB;
		if (LnaGr == 0)
		{
			Edit_SetText(GetDlgItem(h_dialog, IDC_LNAGR), "LNA GR 0 dB");
			Edit_SetText(GetDlgItem(h_dialog, IDC_LNASTATE), "LNA ON");
			//SendMessage(GetDlgItem(hwndDlg, IDC_LNAGR_SWITCH), BM_SETCHECK, BST_CHECKED, 0);
		}
		else
		{
			sprintf_s(str, sizeof(str), "%s %d %s", "LNA GR", LnaGr, "dB");
			Edit_SetText(GetDlgItem(h_dialog, IDC_LNAGR), str);
			Edit_SetText(GetDlgItem(h_dialog, IDC_LNASTATE), "LNA OFF");
			//SendMessage(GetDlgItem(hwndDlg, IDC_LNAGR_SWITCH), BM_SETCHECK, BST_UNCHECKED, 0);
		}

		SystemGainReduction = gRdB + lnaGRdB;
		sprintf_s(str, sizeof(str), "Total System Gain Reduction %d dB", SystemGainReduction);
		Edit_SetText(GetDlgItem(h_dialog, IDC_TOTALGR), str);
	}
	else
	{
		if (gRdB == mir_sdr_ADC_OVERLOAD_DETECTED)
		{
			OutputDebugString("*** ADC OVERLOAD DETECTED ***");
			sprintf_s(str, sizeof(str), "ADC OVERLOAD");
			Edit_SetText(GetDlgItem(h_dialog, IDC_OVERLOAD), str);
			mir_sdr_GainChangeCallbackMessageReceived_fn();
		}
		else
		{
			OutputDebugString("*** ADC OVERLOAD CORRECTED ***");
			sprintf_s(str, sizeof(str), "");
			Edit_SetText(GetDlgItem(h_dialog, IDC_OVERLOAD), str);
			mir_sdr_GainChangeCallbackMessageReceived_fn();
		}
	}
}

void callbackMirSdr(short *xi, short *xq, unsigned int, int, int, int, unsigned int numSamps, unsigned int reset, unsigned int hwRemoved, void *)
{
   (void)hwRemoved;
   int i;
   int j;
   int rem;
   int DataCount = numSamps << 1;
   static unsigned int lastNumSamps = 0;
#ifdef DEBUG_ENABLE
   char m_str[1024];
#endif

   if (reset)
   {
#ifdef DEBUG_ENABLE
      sprintf_s(m_str, sizeof(m_str), "callbackMirSdr: %d", numSamps);
      OutputDebugString(m_str);
#endif
      BufferCounter = 0;
   }

   //Interleave samples
   for (i = 0, j = 0; i < DataCount; i += 2, j++)
   {
      IQBuffer[i]     = xi[j];
      IQBuffer[i + 1] = xq[j];
   }

   //Fill the callback buffer
   for (i = 0; i < DataCount;)
   {
      rem = DataCount - i;
      if ((BufferCounter == 0) && (rem >= buffer_len_samples))
      {
         // no need to copy, if CallbackBuffer empty and i .. i+buffer_len_samples <= DataCount
         // this case looks impossible when DataCount = 504 !
         WinradCallBack(buffer_len_samples / 2, 0, 0, (void*)(&IQBuffer[i]));
         i += buffer_len_samples;
      }
      else if ((BufferCounter + rem) >= buffer_len_samples)
      {
         // CallbackBuffer[] will be filled => and WinradCallBack() can be called
         rem = buffer_len_samples - BufferCounter;
         memcpy(&CallbackBuffer[BufferCounter], &IQBuffer[i], rem << 1);
         WinradCallBack(buffer_len_samples / 2, 0, 0, (void*)CallbackBuffer);
         i += rem;
         BufferCounter = 0;
      }
      else // if (BufferCounter + rem < buffer_len_samples)
      {
         // no chance to fill CallbackBuffer[]
         memcpy(&CallbackBuffer[BufferCounter], &IQBuffer[i], rem << 1);
         BufferCounter += rem;
         i += rem;
      }
   }

   return;
}

extern "C"
bool LIBSDRplay_API __stdcall IQCompensation(bool Enable)
{
   mir_sdr_ErrT Error;
#ifdef DEBUG_ENABLE	
   OutputDebugString("IQCompensation");
   char *errString = NULL;
#endif

   Error = mir_sdr_DCoffsetIQimbalanceControl_fn((unsigned int)Enable, (unsigned int)Enable);						// Reset DC offset correction
   if (Error != mir_sdr_Success)
   {
#ifdef DEBUG_ENABLE
      sprintf_s(errString, 1024, "%s %d: %s", "Enable/Disable DC/IQ Correction Failed\nERROR", Error, getMirErrText(Error));
      OutputDebugString(errString);
#endif
      return false;
   }
   return true;
}

extern "C"
int LIBSDRplay_API __stdcall StartHW(unsigned long freq)
{
	double sampleRateLocal;
#ifdef DEBUG_ENABLE
	OutputDebugString("StartHW");
#endif

	if (freq > 2000000000)
	{
		MessageBox(NULL, "Warning Out of Range", TEXT("WARNING"), MB_OK | MB_SYSTEMMODAL | MB_TOPMOST | MB_ICONEXCLAMATION);
		WinradCallBack(-1, WINRAD_LOCHANGE, 0, NULL);
		return -1;
	}
	if (freq < ((UINT)LowerFqLimit * 1000))
	{
		MessageBox(NULL, "Warning Out of Range", TEXT("WARNING"), MB_OK | MB_SYSTEMMODAL | MB_TOPMOST | MB_ICONEXCLAMATION);
		WinradCallBack(-1, WINRAD_LOCHANGE, 0, NULL);
		return -1;
	}
	mir_sdr_DebugEnable_fn(RSP_DEBUG);
   mir_sdr_LoModeT loMode = GetLoMode();
   mir_sdr_SetLoMode_fn(loMode);
   mir_sdr_SetPpm_fn((double)FqOffsetPPM);
#ifdef DEBUG_ENABLE
   OutputDebugString("Calling mir_sdr_StreamInit_fn");
#endif
   sampleRateLocal = samplerates[SampleRateIdx].value;
   if (IFmodeIdx == 1)
   {
	   if (Bandwidth == mir_sdr_BW_1_536)
		   sampleRateLocal = 8.192;
   }
   mir_sdr_ErrT err = mir_sdr_StreamInit_fn((int *)&GainReduction, sampleRateLocal, Frequency, Bandwidth, IFMode, LNAEnable, &SystemGainReduction, mir_sdr_USE_SET_GR_ALT_MODE, &SampleCount, callbackMirSdr, gaincallbackMirSdr, (void *)NULL);
#ifdef DEBUG_ENABLE
   OutputDebugString("mir_sdr_StreamInit_fn completed");
#endif
   if (err == mir_sdr_Success)
   {
      IQCompensation(PostTunerDcCompensation);
      mir_sdr_SetDcMode_fn(DcCompensationMode, 0);
	  mir_sdr_DecimateControl_fn(DecimateEnable, Decimation[DecimationIdx].DecValue, 0);
      mir_sdr_AgcControl_fn(AGCEnabled, AGCsetpoint, 0, 0, 0, 0, LNAEnable);
      Running = true;
      return buffer_len_samples / 2;
   }
   return 0;
}

extern "C"
long LIBSDRplay_API __stdcall GetHWLO()
{
#ifdef DEBUG_ENABLE
	OutputDebugString("GetHWLO");
#endif
	long returnfreq = (long)(Frequency * 1000000.0 );
	return returnfreq;
}

extern "C"
long LIBSDRplay_API __stdcall GetTune()
{
#ifdef DEBUG_ENABLE
	OutputDebugString("GetTune");
#endif
	long returnfreq = (long)(Frequency * 1000000.0);
	return returnfreq;
}

#pragma comment(lib, "ws2_32.lib") // Winsock Library

void station_copy(station *dest, station *src)
{
	strcpy_s(dest->days, strlen(src->days) + 1, src->days);
	strcpy_s(dest->freq, strlen(src->freq) + 1, src->freq);
	strcpy_s(dest->itu, strlen(src->itu) + 1, src->itu);
	strcpy_s(dest->language, strlen(src->language) + 1, src->language);
	strcpy_s(dest->name, strlen(src->name) + 1, src->name);
	strcpy_s(dest->tac, strlen(src->tac) + 1, src->tac);
	strcpy_s(dest->time, strlen(src->time) + 1, src->time);
}

char * dayOfTheWeek()
{
	const string DAY[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
	char *ret = "\0";
	time_t rawtime;
	tm timeinfo;
	::time(&rawtime);
	gmtime_s(&timeinfo, &rawtime);

	int wday = timeinfo.tm_wday;

	sprintf_s(ret, 4, "%s", DAY[wday]);

	return ret;
}

char * mystrsep(char **stringp, const char *delim)
{
	char *s;
	const char *spanp;
	int c, sc;
	char *tok;

	if ((s = *stringp) == NULL)
		return (NULL);
	for (tok = s;;)
	{
		c = *s++;
		spanp = delim;
		do {
			if ((sc = *spanp++) == c)
			{
				if (c == 0)
					s = NULL;
				else
					s[-1] = 0;
				*stringp = s;
				return (tok);
			}
		} while (sc != 0);
	}
}

char * getfield(char *line, int num)
{
	char *stringp;
	char *token;
	int i = 1;

	stringp = line;
	while (stringp != NULL) {
		token = mystrsep(&stringp, ";");
		if (i == num)
			return token;
		i++;
	}
	return "";
}

bool checkStationTimes(char *min, char *max, char *loc)
{
	int minInt = stoi(min);
	int maxInt = stoi(max);
	int locInt = stoi(loc);
	bool isOn = false;
#ifdef DEBUG_ENABLE
	char tmpString[1024];
	sprintf_s(tmpString, 1024, "min: %d, loc: %d, max: %d", minInt, locInt, maxInt);
	OutputDebugString(tmpString);
#endif

	if (maxInt > minInt)
	{
		if ((locInt >= minInt) && (locInt < maxInt))
			isOn = true;
	}
	else
	{
		if (((locInt >= minInt) && (locInt < 2400)) || ((locInt >= 0) && (locInt < maxInt)))
			isOn = true;
	}
	return isOn;
}

char * parseCSV(long freq)
{
	char line[1024];
	char localFreq[16];
	errno_t err;
	FILE *stream;
	int stationsFound = 0;
	struct station *ptr = NULL, *head = NULL, *curr = NULL;
	struct station *tacPtr = NULL, *tacHead = NULL, *tacCurr = NULL;
	struct station *ituPtr = NULL, *ituHead = NULL, *ituCurr = NULL;
	struct station *timePtr = NULL, *timeHead = NULL, *timeCurr = NULL;
	struct station *dayPtr = NULL, *dayHead = NULL, *dayCurr = NULL;
	char *tacConfig;
	char *ituConfig;
	int tacConfigIndex = 0;
	int ituConfigIndex = 0;
	int tacMatched = 0;
	int ituMatched = 0;
	int timeMatched = 0;
	int dayMatched = 0;
	char *ret = NULL;
	char tacC[10], tacN[10], tacE[10], tacS[10], tacW[10];
	TCHAR csvPath[8192];
	static char retString[1024];

#ifdef DEBUG_ENABLE
	char tmpStringA[1024];
	OutputDebugString("parseCSV");
#endif

	sprintf_s(localFreq, 16, "%d", (int)(freq/1000));
	stationTTipText[0] = NULL;
	displayStationTip = false;

	if (h_StationConfigDialog != NULL)
	{
		tacConfigIndex = ComboBox_GetCurSel(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_TAC));
		ituConfigIndex = ComboBox_GetCurSel(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_ITU));
		tacConfig = targetAreaCodes[tacConfigIndex].targetAreaCode;
		ituConfig = ituCodes[ituConfigIndex].ituCode;
	}
	else
	{
		tacConfig = targetAreaCodes[0].targetAreaCode;
		ituConfig = ituCodes[0].ituCode;
	}

	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, csvPath)))
	{
		PathAppend(csvPath, TEXT("\\SDRplay"));
		PathAppend(csvPath, TEXT("\\sked-b17.csv"));
	}
	else
	{
		MessageBox(NULL, "Error locating local temporary folder", NULL, MB_OK);
		return "DB Error 1";
	}

	if ((err = fopen_s(&stream, csvPath, "r")) != 0)
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("cannot open file");
#endif
		localDBExists = false;
		return "DB Error 2";
	}

	while (fgets(line, 1024, stream))
	{
		char *tmp0 = _strdup(line);
		char *tmp1 = _strdup(line);
		char *tmp2 = _strdup(line);
		char *tmp3 = _strdup(line);
		char *tmp4 = _strdup(line);
		char *tmp5 = _strdup(line);
		char *tmp7 = _strdup(line);
		char *freqField = getfield(tmp0, 1);	// frequency

		if (strcmp(freqField, localFreq) == 0)
		{
			ptr = station_new();
			station_set_tac(ptr, getfield(tmp7, 7));
			station_set_name(ptr, getfield(tmp5, 5));
			station_set_itu(ptr, getfield(tmp4, 4));
			station_set_days(ptr, getfield(tmp3, 3));
			station_set_time(ptr, getfield(tmp2, 2));
			station_set_freq(ptr, getfield(tmp1, 1));

#ifdef DEBUG_ENABLE
			sprintf_s(tmpStringA, 1024, "Freq match: station %d, Freq: %s, Name: %s, TAC: %s", stationsFound, ptr->freq, ptr->name, ptr->tac);
			OutputDebugString(tmpStringA);
#endif
			if (stationsFound == 0)
				head = curr = ptr;
			else
			{
				curr->next = ptr;
				curr = ptr;
			}
			stationsFound++;
		}
		::free(tmp0);
		::free(tmp1);
		::free(tmp2);
		::free(tmp3);
		::free(tmp4);
		::free(tmp5);
		::free(tmp7);
		::free(freqField);
	}
	fclose(stream);

	if (stationsFound == 0)
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("no station found");
#endif
		// no station found, return empty
		return "No Station Found";
	}
	if (strcmp(targetAreaCodes[tacConfigIndex].targetAreaCode, "ZZZ") != 0) // if no TAC selected then skip matching
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("start tac match");
#endif
		sprintf_s(tacC, 5, "C%s", tacConfig);
		sprintf_s(tacN, 5, "N%s", tacConfig);
		sprintf_s(tacE, 5, "E%s", tacConfig);
		sprintf_s(tacS, 5, "S%s", tacConfig);
		sprintf_s(tacW, 5, "W%s", tacConfig);

		ptr = head;
		while (ptr != NULL)
		{
			if ((strcmp(ptr->tac, tacConfig) == 0) || (strcmp(ptr->tac, tacC) == 0) || (strcmp(ptr->tac, tacN) == 0)
				|| (strcmp(ptr->tac, tacE) == 0) || (strcmp(ptr->tac, tacS) == 0) || (strcmp(ptr->tac, tacW) == 0)
				|| (strcmp(ptr->tac, ituConfig) == 0))	// perform TAC match filter
			{
#ifdef DEBUG_ENABLE
				sprintf_s(tmpStringA, 1024, "TAC match: station %d, Freq: %s, Name: %s, TAC: %s", tacMatched, ptr->freq, ptr->name, ptr->tac);
				OutputDebugString(tmpStringA);
#endif
				tacPtr = station_new();
				station_set_tac(tacPtr,  ptr->tac);
				station_set_name(tacPtr, ptr->name);
				station_set_itu(tacPtr,  ptr->itu);
				station_set_days(tacPtr, ptr->days);
				station_set_time(tacPtr, ptr->time);
				station_set_freq(tacPtr, ptr->freq);

				if (tacMatched == 0)
					tacHead = tacCurr = tacPtr;
				else
				{
					tacCurr->next = tacPtr;
					tacCurr = tacPtr;
				}
				tacMatched++;
			}
			ptr = ptr->next;
		}
//			if (tacMatched == 1)
//			{
//#ifdef DEBUG_ENABLE
//				OutputDebugString("only 1 tac match");
//#endif
//				ret = tacHead->name;
////				station_free(tacHead);
////				station_free(tacCurr);
////				station_free(tacPtr);
//				return ret;
//			}
	} // end TAC matching

	if (strcmp(ituCodes[ituConfigIndex].ituCode, "ZZZ") != 0) // if no ITU selewted then skip matching
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("start itu match");
#endif
		if (tacMatched > 0)
			ptr = tacHead;
		else
			ptr = head;

		while (ptr != NULL)
		{
			if (strcmp(ptr->itu, ituConfig) == 0)
			{
				ituPtr = station_new();
				station_set_tac(ituPtr,  ptr->tac);
				station_set_name(ituPtr, ptr->name);
				station_set_itu(ituPtr,  ptr->itu);
				station_set_days(ituPtr, ptr->days);
				station_set_time(ituPtr, ptr->time);
				station_set_freq(ituPtr, ptr->freq);
#ifdef DEBUG_ENABLE
				sprintf_s(tmpStringA, 1024, "ITU match: station %d, Freq: %s, Name: %s, ITU: %s", ituMatched, ptr->freq, ptr->name, ptr->itu);
				OutputDebugString(tmpStringA);
#endif
				if (ituMatched == 0)
					ituHead = ituCurr = ituPtr;
				else
				{
					ituCurr->next = ituPtr;
					ituCurr = ituPtr;
				}
				ituMatched++;
			}
			ptr = ptr->next;
		}
//			if (ituMatched == 1)
//			{
//#ifdef DEBUG_ENABLE
//				OutputDebugString("only 1 itu match");
//#endif
//				ret = ituHead->name;
////				station_free(ituHead);
////				station_free(ituCurr);
////				station_free(ituPtr);
//				return ret;
//			} // end ITU matching
	}

	if (tacMatched > 0)
		ptr = tacHead;
	else if (ituMatched > 0)
		ptr = ituHead;
	else
		ptr = head;
	time_t epoch_time = ::time(NULL);
	tm tm_p;
	::time(&epoch_time);
	gmtime_s(&tm_p, &epoch_time);

	char locTime[5];
	sprintf_s(locTime, 5, "%d%02d", tm_p.tm_hour, tm_p.tm_min);

#ifdef DEBUG_ENABLE
	OutputDebugString(locTime);
#endif
	while (ptr != NULL)
	{
		station_set_MinMaxTimes(ptr, ptr->time);

		if (checkStationTimes(ptr->minTime, ptr->maxTime, locTime))
		{
			timePtr = station_new();
			station_set_tac(timePtr, ptr->tac);
			station_set_name(timePtr, ptr->name);
			station_set_itu(timePtr, ptr->itu);
			station_set_days(timePtr, ptr->days);
			station_set_time(timePtr, ptr->time);
			station_set_freq(timePtr, ptr->freq);
#ifdef DEBUG_ENABLE
			sprintf_s(tmpStringA, 1024, "Time match: station %d, Freq: %s, Name: %s, Time Min: %s, Loc Time: %s, Time Max: %s", timeMatched, ptr->freq, ptr->name, ptr->minTime, locTime, ptr->maxTime);
			OutputDebugString(tmpStringA);
#endif
			if (timeMatched == 0)
				timeHead = timeCurr = timePtr;
			else
			{
				timeCurr->next = timePtr;
				timeCurr = timePtr;
			}
			timeMatched++;
		}
		ptr = ptr->next;
		}

	if (timeMatched == 0)
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("no station found");
#endif
		// no station found, return empty
		return "No Station Found";
	}
	else if (timeMatched == 1)
	{
#ifdef DEBUG_ENABLE
		OutputDebugString("only 1 time match");
#endif
		ret = timeHead->name;
//			station_free(timeHead);
//			station_free(timeCurr);
//			station_free(timePtr);
		return ret;
	} // end time matching

	// day match - disable for now
	bool runNow = false;
	if (runNow)
	{
		ptr = timeHead;

		while (ptr != NULL)
		{
			if (strlen(ptr->days) < 1)
			{
				dayPtr = station_new();
				station_set_tac(dayPtr, ptr->tac);
				station_set_name(dayPtr, ptr->name);
				station_set_itu(dayPtr, ptr->itu);
				station_set_days(dayPtr, ptr->days);
				station_set_time(dayPtr, ptr->time);
				station_set_freq(dayPtr, ptr->freq);
#ifdef DEBUG_ENABLE
				sprintf_s(tmpStringA, 1024, "Day match: station %d, Freq: %s, Name: %s, Days: %s", dayMatched, ptr->freq, ptr->name, ptr->days);
				OutputDebugString(tmpStringA);
#endif
				if (dayMatched == 0)
					dayHead = dayCurr = dayPtr;
				else
				{
					dayCurr->next = dayPtr;
					dayCurr = dayPtr;
			}
				dayMatched++;
	}
			ptr = ptr->next;
		}

		if (dayMatched == 0)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("no station found");
#endif
			// no station found, return empty
			return "No Station Found";
		}
		else if (dayMatched == 1)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("only 1 day match");
#endif
			ret = dayHead->name;
//				station_free(dayHead);
//				station_free(dayCurr);
//				station_free(dayPtr);
			return ret;
		} // end day matching
	}

#ifdef DEBUG_ENABLE
	OutputDebugString("last resort");
#endif
	if (timeMatched > 1)
	{
		sprintf_s(retString, 1024, "%d stations found", timeMatched);
		displayStationTip = true;

		ptr = timeHead;
		int count = 0;
		while (ptr != NULL)
		{
			if (count != 0)
				strcat_s(stationTTipText, sizeof(stationTTipText), ", ");
			strcat_s(stationTTipText, sizeof(stationTTipText), ptr->name);
			ptr = ptr->next;
			count++;
		}
		return retString;
	}
	else
	{
		ret = timeHead->name;
//			station_free(timeHead);
//			station_free(timeCurr);
//			station_free(timePtr);
		return ret; // last resort, pick the first station in the time list
	}
//	}
}

extern "C"
void LIBSDRplay_API __stdcall TuneChanged(long vfoFreq)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("TuneChanged");
#endif
	char stationName[256];
	static HWND hwndTip1 = NULL;
	static HWND hwndTip2 = NULL;
	static HWND hwndTool1 = NULL;
	static HWND hwndTool2 = NULL;
	static TOOLINFO toolInfo1;
	static TOOLINFO toolInfo2;
	char *result;

	if (stationLookup && localDBExists)
	{
		result = parseCSV(vfoFreq);
		sprintf_s(stationName, 256, "Station: %s", result);
#ifdef DEBUG_ENABLE
		OutputDebugString(stationName);
#endif
		if (h_dialog != NULL)
		{
			hwndTool1 = GetDlgItem(h_dialog, IDC_STATIONNAME);
			Edit_SetText(hwndTool1, stationName);
		}

		if (h_StationDialog != NULL)
		{
			hwndTool2 = GetDlgItem(h_StationDialog, IDC_STATION_LABEL);
			Edit_SetText(hwndTool2, stationName);
		}

		if (displayStationTip && (hwndTip1 == NULL))
		hwndTip1 = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			h_dialog, NULL, hInst, NULL);
		if (displayStationTip && (hwndTip2 == NULL))
		hwndTip2 = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			h_StationDialog, NULL, hInst, NULL);

		toolInfo1 = { 0 };
		toolInfo1.cbSize = sizeof(toolInfo1);
		toolInfo1.hwnd = h_dialog;
		toolInfo1.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		toolInfo1.uId = (UINT_PTR)hwndTool1;
		toolInfo1.lpszText = stationTTipText;
		if (displayStationTip)
		{
			SendMessage(hwndTip1, TTM_ADDTOOL, 0, (LPARAM)&toolInfo1);
		}
		else
		{
			SendMessage(hwndTip1, TTM_DELTOOL, 0, (LPARAM)&toolInfo1);
			DestroyWindow(hwndTip1);
			hwndTip1 = NULL;
		}

		toolInfo2 = { 0 };
		toolInfo2.cbSize = sizeof(toolInfo2);
		toolInfo2.hwnd = h_StationDialog;
		toolInfo2.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		toolInfo2.uId = (UINT_PTR)hwndTool2;
		toolInfo2.lpszText = stationTTipText;
		if (displayStationTip)
		{
			SendMessage(hwndTip2, TTM_ADDTOOL, 0, (LPARAM)&toolInfo2);
		}
		else
		{
			SendMessage(hwndTip2, TTM_DELTOOL, 0, (LPARAM)&toolInfo2);
			DestroyWindow(hwndTip2);
			hwndTip2 = NULL;
		}
	}
	else
	{
		sprintf_s(stationName, 256, "Station: Disabled");
		if (h_dialog != NULL)
			Edit_SetText(GetDlgItem(h_dialog, IDC_STATIONNAME), stationName);
		if (h_StationDialog != NULL)
			Edit_SetText(GetDlgItem(h_StationDialog, IDC_STATION_LABEL), stationName);
	}
}

extern "C"
char LIBSDRplay_API __stdcall GetMode(void)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("GetMode");
#endif
	return demodMode;
}

extern "C"
void LIBSDRplay_API __stdcall ModeChanged(char)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("ModeChanged");
#endif
	return;
}

extern "C"
long LIBSDRplay_API __stdcall GetHWSR()
{
	TCHAR str[255];
#ifdef DEBUG_ENABLE
	OutputDebugString("GetHWSR");
	sprintf_s(str, 255, "SampleRateIdx: %d", SampleRateIdx);
	OutputDebugString(str);
#endif
	long SR = (long)(2.0 * 1e6);

	if (IFmodeIdx == 1)
	{
		if (samplerates[SampleRateIdx].value == 8.0)
			SR = (long)(8.192 * 1e6);
		else
			SR = (long)(samplerates[SampleRateIdx].value * 1e6);

		if ((((samplerates[SampleRateIdx].value == 8.0) && (Bandwidth == mir_sdr_BW_1_536) && (IFMode == mir_sdr_IF_2_048)) ||
			 ((samplerates[SampleRateIdx].value == 2.0) && (Bandwidth == mir_sdr_BW_0_200) && (IFMode == mir_sdr_IF_0_450)) ||
			 ((samplerates[SampleRateIdx].value == 2.0) && (Bandwidth == mir_sdr_BW_0_300) && (IFMode == mir_sdr_IF_0_450)))
			&& down_conversion_enabled)
		{
			SR = SR >> 2;
		}

		if ((samplerates[SampleRateIdx].value == 2.0) && (Bandwidth == mir_sdr_BW_0_600) && (IFMode == mir_sdr_IF_0_450)
			&& down_conversion_enabled)
		{
			SR = SR >> 1;
		}
	}
	else
	{
		SR = (long)(samplerates[SampleRateIdx].value * 1e6);
		if (DecimateEnable == 1 && (DecimationIdx > 0))
		{
			SR = SR / Decimation[DecimationIdx].DecValue;
		}
	}
#ifdef DEBUG_ENABLE
	sprintf_s(str, 255, "SR: %d", SR);
	OutputDebugString(str);
#endif
	sprintf_s(str, sizeof(str), "%.2f MHz", (SR / 1e6));
	Edit_SetText(GetDlgItem(h_dialog, IDC_FINALSR), str);
	return SR;
}

extern "C"
int LIBSDRplay_API __stdcall ExtIoGetSrates(int srate_idx, double *samplerate)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("ExtIoGetSrates");
#endif
	if (srate_idx < (sizeof(samplerates) / sizeof(samplerates[0])))
	{
		*samplerate = samplerates[srate_idx].value * 1000000;
		return 0;
	}
	return 1;	// ERROR
}

extern "C"
int  LIBSDRplay_API __stdcall ExtIoGetActualSrateIdx(void)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("ExtIoGetActualSrateIdx");
#endif
	return 0;
}

extern "C"
int  LIBSDRplay_API __stdcall ExtIoSetSrate(int)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("ExtIoSetSrate");
#endif
	return 1;	// ERROR
}

extern "C"
int  LIBSDRplay_API __stdcall GetAttenuators(int atten_idx, float *attenuation)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("GetAttenuators");
#endif
	if (atten_idx < n_gains)
	{
		*attenuation = gains[atten_idx] * 1.0f;
		return 0;
	}
	return 1;	// End or Error
}

extern "C"
int  LIBSDRplay_API __stdcall GetActualAttIdx(void)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("GetActualAttIdx");
#endif
	for (int i = 0; i < n_gains; i++)
		if (last_gain == gains[i])
			return i;
	return -1;
}

extern "C"
int  LIBSDRplay_API __stdcall SetAttenuator(int atten_idx)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("SetAttenuator");
#endif
	if (atten_idx < 0 || atten_idx > n_gains)
		return -1;

	int pos = gains[atten_idx];

	if (AGCEnabled == mir_sdr_AGC_DISABLE)
	{
		if (pos != last_gain)
		{
			GainReduction = pos;
			sprintf_s(GainReductionTxt, 254, "%d", GainReduction);
			Edit_SetText(GetDlgItem(h_dialog, IDC_GR), GainReductionTxt);
			SendMessage(GetDlgItem(h_dialog, IDC_GAINSLIDER), TBM_SETPOS, (WPARAM)true, (LPARAM)GainReduction);
			last_gain = pos;
		}
	}
	return 0;
}

extern "C"
int   LIBSDRplay_API __stdcall ExtIoGetAGCs(int, char *)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("ExtIoGetAGCs");
#endif
	//To DO
	return 1;	// ERROR
}

extern "C"
int   LIBSDRplay_API __stdcall ExtIoGetActualAGCidx(void)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("ExtIoGetActualAGCidx");
#endif
	// To do
	 return 0;
}

extern "C"
int   LIBSDRplay_API __stdcall ExtIoSetAGC(int)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("ExtIoSetAGC");
#endif
	//To do
	return 1;	// ERROR
}

extern "C"
int   LIBSDRplay_API __stdcall ExtIoGetSetting(int idx, char *description, char *value)
{
#ifdef DEBUG_ENABLE
	OutputDebugString("ExtIoGetSetting");
#endif
	switch (idx)
	{
		case 0:
			sprintf_s(description, 1024, "%s", "SampleRateIdx");
			sprintf_s(value, 1024, "%d", SampleRateIdx);
			return 0;
		case 1:
			sprintf_s(description, 1024, "%s", "Tuner AGC");
			sprintf_s(value, 1024, "%d", 0);
			return 0;
		case 2:
			sprintf_s(description, 1024, "%s", "RTL_AGC");
			sprintf_s(value, 1024, "%d", 0);
			return 0;
		case 3:
			sprintf_s(description, 1024, "%s", "Frequency_Correction");
			sprintf_s(value, 1024, "%d", ppm_default);		// TODO: expecting int?? FqOffsetPPM??
			return 0;
		case 4:
			sprintf_s(description, 1024, "%s", "Tuner_Gain");
			sprintf_s(value, 1024, "%d", GainReduction);	// TODO: Gain Reduction vs Gain?? LNA??
			return 0;
		case 5:
			sprintf_s(description, 1024, "%s", "Buffer_Size");
			sprintf_s(value, 1024, "%d", buffer_default);	// TODO: correct??
			return 0;
		case 6:
			sprintf_s(description, 1024, "%s", "Offset_Tuning");
			sprintf_s(value, 1024, "%d", 0);	// TODO: correct??
			return 0;
		case 7:
			sprintf_s(description, 1024, "%s", "Direct_Sampling");
			sprintf_s(value, 1024, "%d", 0);	// TODO: correct??
			return 0;
		case 8:
			sprintf_s(description, 1024, "%s", "Device");
			sprintf_s(value, 1024, "%d", 0); // TODO: only one device supported
			return 0;
	}
	return -1; // ERROR
}

extern "C"
void  LIBSDRplay_API __stdcall ExtIoSetSetting(int idx, const char *value)
{
	int tempInt;
#ifdef DEBUG_ENABLE
	OutputDebugString("ExtIoSetSetting");
#endif
	switch (idx)
	{
	case 0: // Sample Rate
		SampleRateIdx = 0;
		tempInt = atoi(value);
		if (tempInt >= 0 && tempInt <= (sizeof(samplerates) / sizeof(samplerates[0])))
		{
			SampleRateIdx = tempInt;
		}
		break;
	case 1:	// Tuner AGC
		tempInt = atoi(value);
		break;
	case 2:	// RTL AGC
		tempInt = atoi(value);
		break;
	case 3:	// PPM
		tempInt = atoi(value);
		ppm_default = tempInt;
		break;
	case 4:	// Gain
		tempInt = atoi(value);
		GainReduction = tempInt;
		break;
	case 5:	// Buffer Size
		tempInt = atoi(value);
		buffer_default = tempInt;
		break;
	case 6:	// Offset Freq
		tempInt = atoi(value);
		break;
	case 7:	// Direct Sampling
		tempInt = atoi(value);
		break;
	case 8:	// Device
		tempInt = atoi(value);
		break;
	}
}

extern "C"
void LIBSDRplay_API __stdcall StopHW()
{
#ifdef DEBUG_ENABLE
	OutputDebugString("StopHW");
#endif
	Running = false;
	mir_sdr_StreamUninit_fn();
}

extern "C"
void LIBSDRplay_API __stdcall CloseHW()
{
#ifdef DEBUG_ENABLE
	OutputDebugString("CloseHW");
#endif
	SaveSettings();
	if (h_dialog != NULL)
		DestroyWindow(h_dialog);
	if (h_AdvancedDialog != NULL)
		DestroyWindow(h_AdvancedDialog);
	if (h_ProfilesDialog != NULL)
		DestroyWindow(h_ProfilesDialog);
	if (h_HelpDialog != NULL)
		DestroyWindow(h_HelpDialog);
	if (h_StationDialog != NULL)
		DestroyWindow(h_StationDialog);
	if (h_StationConfigDialog != NULL)
		DestroyWindow(h_StationConfigDialog);
	mir_sdr_ReleaseDeviceIdx_fn();
}

extern "C"
void LIBSDRplay_API __stdcall ShowGUI() // Hotkeys defined here
{
#ifdef DEBUG_ENABLE
	OutputDebugString("ShowGUI");
#endif
	if (h_dialog == NULL)
	{
		h_dialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_SDRPLAY_SETTINGS), NULL, (DLGPROC)MainDlgProc);
	}
	if (!definedHotkeys)
	{
		NUM_OF_HOTKEYS = 15;
		RegisterHotKey(h_dialog, 1, MOD_ALT, 0x41);			// ALT-A for AGC enable toggle
		RegisterHotKey(h_dialog, 2, NULL, 0x6D);			// - for Gain decrease (Gain reduction increase) (KEYPAD)
		RegisterHotKey(h_dialog, 3, NULL, 0x6B);			// + for Gain increase (Gain reduction decrease) (KEYPAD)
		RegisterHotKey(h_dialog, 4, NULL, 0xBD);			// - for Gain decrease (Gain reduction increase)
		RegisterHotKey(h_dialog, 5, MOD_SHIFT, 0xBB);		// SHIFT-+ for Gain increase (Gain reduction decrease)
		RegisterHotKey(h_dialog, 6, MOD_SHIFT, 0x70);		// SHIFT+F1 for loading stored profile 1
		RegisterHotKey(h_dialog, 7, MOD_SHIFT, 0x71);		// SHIFT+F2 for loading stored profile 2
		RegisterHotKey(h_dialog, 8, MOD_SHIFT, 0x72);		// SHIFT+F3 for loading stored profile 3
		RegisterHotKey(h_dialog, 9, MOD_SHIFT, 0x73);		// SHIFT+F4 for loading stored profile 4
		RegisterHotKey(h_dialog, 10, MOD_SHIFT, 0x74);		// SHIFT+F5 for loading stored profile 5
		RegisterHotKey(h_dialog, 11, MOD_SHIFT, 0x75);		// SHIFT+F6 for loading stored profile 6
		RegisterHotKey(h_dialog, 12, MOD_SHIFT, 0x76);		// SHIFT+F7 for loading stored profile 7
		RegisterHotKey(h_dialog, 13, MOD_SHIFT, 0x77);		// SHIFT+F8 for loading stored profile 8
		RegisterHotKey(h_dialog, 14, MOD_ALT, 0x50);		// ALT-P for Profile panel view toggle
		RegisterHotKey(h_dialog, 15, MOD_ALT, 0x58);		// ALT-X for EXTIO panel view toggle
		definedHotkeys = true;
	}
	ShowWindow(h_dialog, SW_SHOW);
	BringWindowToTop(h_dialog);
}

extern "C"
void LIBSDRplay_API  __stdcall HideGUI()
{
#ifdef DEBUG_ENABLE
	OutputDebugString("HideGUI");
#endif
	int i;
	for (i = 1; i <= NUM_OF_HOTKEYS; i++)
	{
		UnregisterHotKey(h_dialog, i);
	}
	ShowWindow(h_dialog,SW_HIDE);
	definedHotkeys = false;
}

extern "C"
void LIBSDRplay_API  __stdcall SwitchGUI() // Hotkeys defined here
{
#ifdef DEBUG_ENABLE
	OutputDebugString("SwitchGUI");
#endif
	if (IsWindowVisible(h_dialog))
	{
		int i;
		for (i = 1; i <= NUM_OF_HOTKEYS; i++)
		{
			UnregisterHotKey(h_dialog, i);
		}
		ShowWindow(h_dialog, SW_HIDE);
		definedHotkeys = false;
	}
	else
	{
		if (!definedHotkeys)
		{
			NUM_OF_HOTKEYS = 15;
			RegisterHotKey(h_dialog, 1, MOD_ALT, 0x41);			// ALT-A for AGC enable toggle
			RegisterHotKey(h_dialog, 2, NULL, 0x6D);			// - for Gain decrease (Gain reduction increase) (KEYPAD)
			RegisterHotKey(h_dialog, 3, NULL, 0x6B);			// + for Gain increase (Gain reduction decrease) (KEYPAD)
			RegisterHotKey(h_dialog, 4, NULL, 0xBD);			// - for Gain decrease (Gain reduction increase)
			RegisterHotKey(h_dialog, 5, MOD_SHIFT, 0xBB);		// SHIFT-+ for Gain increase (Gain reduction decrease)
			RegisterHotKey(h_dialog, 6, MOD_SHIFT, 0x70);		// SHIFT+F1 for loading stored profile 1
			RegisterHotKey(h_dialog, 7, MOD_SHIFT, 0x71);		// SHIFT+F2 for loading stored profile 2
			RegisterHotKey(h_dialog, 8, MOD_SHIFT, 0x72);		// SHIFT+F3 for loading stored profile 3
			RegisterHotKey(h_dialog, 9, MOD_SHIFT, 0x73);		// SHIFT+F4 for loading stored profile 4
			RegisterHotKey(h_dialog, 10, MOD_SHIFT, 0x74);		// SHIFT+F5 for loading stored profile 5
			RegisterHotKey(h_dialog, 11, MOD_SHIFT, 0x75);		// SHIFT+F6 for loading stored profile 6
			RegisterHotKey(h_dialog, 12, MOD_SHIFT, 0x76);		// SHIFT+F7 for loading stored profile 7
			RegisterHotKey(h_dialog, 13, MOD_SHIFT, 0x77);		// SHIFT+F8 for loading stored profile 8
			RegisterHotKey(h_dialog, 14, MOD_ALT, 0x50);		// ALT-P for Profile panel view toggle
			RegisterHotKey(h_dialog, 15, MOD_ALT, 0x58);		// ALT-X for EXTIO panel view toggle
			definedHotkeys = true;
		}
		ShowWindow(h_dialog, SW_SHOW);
		BringWindowToTop(h_dialog);
	}
}

extern "C"
void LIBSDRplay_API __stdcall SetCallback(void(*myCallBack)(int, int, float, void *))
{
#ifdef DEBUG_ENABLE
	OutputDebugString("SetCallback");
#endif
	WinradCallBack = myCallBack;
}

extern "C"
int LIBSDRplay_API __stdcall GetStatus()
{
#ifdef DEBUG_ENABLE
	OutputDebugString("GetStatus");
#endif
	return 0;
}

int FindSampleRateIdx(double WantedSampleRate)
{
	int Found, i;
	int SRiD = 0;
	double SR;

	Found = 0;
	for (i = 0; i < (sizeof(samplerates) / sizeof(samplerates[0])); i++)
	{
		SR = (samplerates[i].value) + 0.06;
		if (SR >  WantedSampleRate && Found == 0)
		{
			SRiD = i;
			Found = 1;
		}
	}
	return SRiD;
}

void SaveSettings()
{
	HKEY Settingskey;
	int error, AGC, LOAuto, PostDcComp, LNAEN, SL, tempVar;
	DWORD dwDisposition;

	error = RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\SDRplay\\Settings"), 0, KEY_ALL_ACCESS, &Settingskey);
	if (error != ERROR_SUCCESS)
	{ 
		if (error == ERROR_FILE_NOT_FOUND)
		{
			//Need to create registry entry.
			error = RegCreateKeyEx(HKEY_CURRENT_USER, TEXT("Software\\SDRplay\\Settings"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Settingskey, &dwDisposition);
		}
		else
		{
			MessageBox(NULL, "Failed to locate Settings registry entry", NULL, MB_OK);
		}
	}

	error = RegSetValueEx(Settingskey, TEXT("DcCompensationMode"), 0, REG_DWORD, (LPBYTE)&DcCompensationMode, sizeof(DcCompensationMode));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save DcCompensationMode", NULL, MB_OK);

	error = RegSetValueEx(Settingskey, TEXT("RefreshPeriodMemory"), 0, REG_DWORD, (LPBYTE)&RefreshPeriodMemory, sizeof(RefreshPeriodMemory));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save RefreshPeriodMemory", NULL, MB_OK);

	error = RegSetValueEx(Settingskey, TEXT("TrackingPeriod"), 0, REG_DWORD, (LPBYTE)&TrackingPeriod, sizeof(TrackingPeriod));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save TrackingPeriod", NULL, MB_OK);

	error = RegSetValueEx(Settingskey, TEXT("FreqTrim"), 0, REG_DWORD, (LPBYTE)&FqOffsetPPM, sizeof(FqOffsetPPM));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save Frequenct Trim", NULL, MB_OK);

	error = RegSetValueEx(Settingskey, TEXT("IFmodeIdx"), 0, REG_DWORD, (LPBYTE)&IFmodeIdx, sizeof(IFmodeIdx));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save IF Mode", NULL, MB_OK);

	error = RegSetValueEx(Settingskey, TEXT("BandwidthIdx"), 0, REG_DWORD, (LPBYTE)&BandwidthIdx, sizeof(BandwidthIdx));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save IF Bandwidth", NULL, MB_OK);

	error = RegSetValueEx(Settingskey, TEXT("SampleRateIdx"), 0, REG_DWORD, (LPBYTE)&SampleRateIdx, sizeof(SampleRateIdx));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save Sample Rate", NULL, MB_OK);

	error = RegSetValueEx(Settingskey, TEXT("DecimationIdx"), 0, REG_DWORD, (LPBYTE)&DecimationIdx, sizeof(DecimationIdx));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save Decimation Factor", NULL, MB_OK);

	error = RegSetValueEx(Settingskey, TEXT("AGCsetpoint"), 0, REG_DWORD, (LPBYTE)&AGCsetpoint, sizeof(AGCsetpoint));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save AGC Setpoint", NULL, MB_OK);

	error = RegSetValueEx(Settingskey, TEXT("GainReduction"), 0, REG_DWORD, (LPBYTE)&GainReduction, sizeof(GainReduction));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save Gain Reduction", NULL, MB_OK);

	error = RegSetValueEx(Settingskey, TEXT("LOplan"), 0, REG_DWORD, (LPBYTE)&LOplan, sizeof(LOplan));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save LO Plan", NULL, MB_OK);

	error = RegSetValueEx(Settingskey, TEXT("Frequency"), 0, REG_BINARY, (LPBYTE)&Frequency, sizeof(Frequency));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save Frequency", NULL, MB_OK);

	if (AGCEnabled == mir_sdr_AGC_DISABLE)
		AGC = 0;
	else
		AGC = 1;
	error = RegSetValueEx(Settingskey, TEXT("AGCEnabled"), 0, REG_DWORD, (LPBYTE)&AGC, sizeof(AGC));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save AGC Setting", NULL, MB_OK);

	if (stationLookup)
		SL = 1;
	else
		SL = 0;
	error = RegSetValueEx(Settingskey, TEXT("StationLookup"), 0, REG_DWORD, (LPBYTE)&SL, sizeof(SL));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save StationLookup Setting", NULL, MB_OK);

	if (workOffline)
		SL = 1;
	else
		SL = 0;
	error = RegSetValueEx(Settingskey, TEXT("WorkOffline"), 0, REG_DWORD, (LPBYTE)&SL, sizeof(SL));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save WorkOffline Setting", NULL, MB_OK);

	if (h_StationConfigDialog != NULL)
	{
		tempVar = ComboBox_GetCurSel(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_TAC));
		error = RegSetValueEx(Settingskey, TEXT("TACIndex"), 0, REG_DWORD, (LPBYTE)&tempVar, sizeof(tempVar));
		if (error != ERROR_SUCCESS)
			MessageBox(NULL, "Failed to save TAC Setting", NULL, MB_OK);

		tempVar = ComboBox_GetCurSel(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_ITU));
		error = RegSetValueEx(Settingskey, TEXT("ITUIndex"), 0, REG_DWORD, (LPBYTE)&tempVar, sizeof(tempVar));
		if (error != ERROR_SUCCESS)
			MessageBox(NULL, "Failed to save ITU Setting", NULL, MB_OK);
	}
	
	if (LOplanAuto)
		LOAuto = 1;
	else
		LOAuto = 0;
	error = RegSetValueEx(Settingskey, TEXT("LOAuto"), 0, REG_DWORD, (LPBYTE)&LOAuto, sizeof(LOAuto));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save LO Auto Setting", NULL, MB_OK);

	if (PostTunerDcCompensation)
		PostDcComp = 1;
	else
		PostDcComp = 0;
	error = RegSetValueEx(Settingskey, TEXT("PostDCComp"), 0, REG_DWORD, (LPBYTE)&PostDcComp, sizeof(PostDcComp));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save Post DC Compensation Setting", NULL, MB_OK);

	if (LNAEnable)
		LNAEN = 1;
	else
		LNAEN = 0;
	error = RegSetValueEx(Settingskey, TEXT("LNAEnabled"), 0, REG_DWORD, (LPBYTE)&LNAEN, sizeof(LNAEN));
	if (error != ERROR_SUCCESS)
		MessageBox(NULL, "Failed to save LNA Setting", NULL, MB_OK);


	if (_tcscmp(p1fname, _T("")) != 0)
	{
		error = RegSetValueEx(Settingskey, TEXT("P1"), 0, REG_SZ, (LPBYTE)&p1fname, sizeof(p1fname));
		if (error != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Failed to save P1 setting", NULL, MB_OK);
		}
	}
	else
	{
		error = RegDeleteValue(Settingskey, TEXT("P1"));
	}

	if (_tcscmp(p2fname, _T("")) != 0)
	{
		error = RegSetValueEx(Settingskey, TEXT("P2"), 0, REG_SZ, (LPBYTE)&p2fname, sizeof(p2fname));
		if (error != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Failed to save P2 setting", NULL, MB_OK);
		}
	}
	else
	{
		error = RegDeleteValue(Settingskey, TEXT("P2"));
	}

	if (_tcscmp(p3fname, _T("")) != 0)
	{
		error = RegSetValueEx(Settingskey, TEXT("P3"), 0, REG_SZ, (LPBYTE)&p3fname, sizeof(p3fname));
		if (error != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Failed to save P3 setting", NULL, MB_OK);
		}
	}
	else
	{
		error = RegDeleteValue(Settingskey, TEXT("P3"));
	}

	if (_tcscmp(p4fname, _T("")) != 0)
	{
		error = RegSetValueEx(Settingskey, TEXT("P4"), 0, REG_SZ, (LPBYTE)&p4fname, sizeof(p4fname));
		if (error != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Failed to save P4 setting", NULL, MB_OK);
		}
	}
	else
	{
		error = RegDeleteValue(Settingskey, TEXT("P4"));
	}

	if (_tcscmp(p5fname, _T("")) != 0)
	{
		error = RegSetValueEx(Settingskey, TEXT("P5"), 0, REG_SZ, (LPBYTE)&p5fname, sizeof(p5fname));
		if (error != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Failed to save P5 setting", NULL, MB_OK);
		}
	}
	else
	{
		error = RegDeleteValue(Settingskey, TEXT("P5"));
	}

	if (_tcscmp(p6fname, _T("")) != 0)
	{
		error = RegSetValueEx(Settingskey, TEXT("P6"), 0, REG_SZ, (LPBYTE)&p6fname, sizeof(p6fname));
		if (error != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Failed to save P6 setting", NULL, MB_OK);
		}
	}
	else
	{
		error = RegDeleteValue(Settingskey, TEXT("P6"));
	}

	if (_tcscmp(p7fname, _T("")) != 0)
	{
		error = RegSetValueEx(Settingskey, TEXT("P7"), 0, REG_SZ, (LPBYTE)&p7fname, sizeof(p7fname));
		if (error != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Failed to save P7 setting", NULL, MB_OK);
		}
	}
	else
	{
		error = RegDeleteValue(Settingskey, TEXT("P7"));
	}

	if (_tcscmp(p8fname, _T("")) != 0)
	{
		error = RegSetValueEx(Settingskey, TEXT("P8"), 0, REG_SZ, (LPBYTE)&p8fname, sizeof(p8fname));
		if (error != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Failed to save P8 setting", NULL, MB_OK);
		}
	}
	else
	{
		error = RegDeleteValue(Settingskey, TEXT("P8"));
	}

	RegCloseKey(Settingskey);
}

void LoadSettings()
{
	HKEY Settingskey;
	int error, tempRB;
	DWORD DoubleSz, IntSz;

	DoubleSz = sizeof(double);
	IntSz = sizeof(int);

	error = RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\SDRplay\\Settings"), 0, KEY_ALL_ACCESS, &Settingskey);
	if (error == ERROR_SUCCESS)
	{
		error = RegQueryValueEx(Settingskey, "LowerFqLimit", NULL, NULL, (LPBYTE)&LowerFqLimit, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Lower Frequency Limit set to 10kHz");
#endif
			LowerFqLimit = 10;
		}

		error = RegQueryValueEx(Settingskey, "DcCompensationMode", NULL, NULL, (LPBYTE)&DcCompensationMode, &IntSz);
		if (error != ERROR_SUCCESS)
			{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall DcCompensationMode");
#endif
			DcCompensationMode = 3;
			}

		error = RegQueryValueEx(Settingskey, "RefreshPeriodMemory", NULL, NULL, (LPBYTE)&RefreshPeriodMemory, &IntSz);
		if (error != ERROR_SUCCESS)
			{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall Refresh Period");
#endif
			RefreshPeriodMemory = 2;
			}

		error = RegQueryValueEx(Settingskey, "TrackingPeriod", NULL, NULL, (LPBYTE)&TrackingPeriod, &IntSz);
		if (error != ERROR_SUCCESS)
			{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall DC Compensation Tracking Period");
#endif
			TrackingPeriod = 20;
			}

		error = RegQueryValueEx(Settingskey, "Frequency", NULL, NULL, (LPBYTE)&Frequency, &DoubleSz);
		if (error != ERROR_SUCCESS)
			{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved frequency state");
#endif
			Frequency = 98.8;
			}

		error = RegQueryValueEx(Settingskey, "LOplan", NULL, NULL, (LPBYTE)&LOplan, &IntSz);
		if (error != ERROR_SUCCESS)
			{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved LO Plan");
#endif
			LOplan = LO120MHz;
			}

		error = RegQueryValueEx(Settingskey, "GainReduction", NULL, NULL, (LPBYTE)&GainReduction, &IntSz);
		if (error != ERROR_SUCCESS)
			{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved Gain Reduction");
#endif
			GainReduction = 50;
			}

		error = RegQueryValueEx(Settingskey, "AGCsetpoint", NULL, NULL, (LPBYTE)&AGCsetpoint, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved AGC Setpoint");
#endif
			AGCsetpoint = -30;
		}

		error = RegQueryValueEx(Settingskey, "SampleRateIdx", NULL, NULL, (LPBYTE)&SampleRateIdx, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved Sample Rate");
#endif
			SampleRateIdx = 0;
		}

		error = RegQueryValueEx(Settingskey, "DecimationIdx", NULL, NULL, (LPBYTE)&DecimationIdx, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved Decimation Factor");
#endif
			DecimationIdx = 0;
		}

		error = RegQueryValueEx(Settingskey, "BandwidthIdx", NULL, NULL, (LPBYTE)&BandwidthIdx, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved IF Bandwidth");
#endif
			BandwidthIdx = 3;
		}

		error = RegQueryValueEx(Settingskey, "IFmodeIdx", NULL, NULL, (LPBYTE)&IFmodeIdx, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved IF mode");
#endif
			IFmodeIdx = 0;
		}

		error = RegQueryValueEx(Settingskey, "FreqTrim", NULL, NULL, (LPBYTE)&FqOffsetPPM, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved FreqTrim");
#endif
			FqOffsetPPM = 0;
		}

		tempRB = 0;
		error = RegQueryValueEx(Settingskey, "AGCEnabled", NULL, NULL, (LPBYTE)&tempRB, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved AGCEnabled state");
#endif
			AGCEnabled = mir_sdr_AGC_5HZ;
		}
		else
		{
			if (tempRB == 1)
				AGCEnabled = mir_sdr_AGC_5HZ;
			else
				AGCEnabled = mir_sdr_AGC_DISABLE;
		}

		tempRB = 0;
		error = RegQueryValueEx(Settingskey, "StationLookup", NULL, NULL, (LPBYTE)&tempRB, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved StationLookup state");
#endif
			stationLookup = false;
		}
		else
		{
			if (tempRB == 1)
				stationLookup = true;
			else
				stationLookup = false;
		}

		tempRB = 0;
		error = RegQueryValueEx(Settingskey, "WorkOffline", NULL, NULL, (LPBYTE)&tempRB, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved WorkOffline state");
#endif
			workOffline = false;
		}
		else
		{
			if (tempRB == 1)
				workOffline = true;
			else
				workOffline = false;
		}

		tempRB = 0;
		error = RegQueryValueEx(Settingskey, "TACIndex", NULL, NULL, (LPBYTE)&tempRB, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved TAC setting");
#endif
			TACIndex = 0;
			if (h_StationConfigDialog != NULL)
				ComboBox_SetCurSel(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_TAC), TACIndex);
		}
		else
		{
			TACIndex = tempRB;
			if (h_StationConfigDialog != NULL)
				ComboBox_SetCurSel(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_TAC), TACIndex);
		}

		tempRB = 0;
		error = RegQueryValueEx(Settingskey, "ITUIndex", NULL, NULL, (LPBYTE)&tempRB, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved ITU setting");
#endif
			ITUIndex = 0;
			if (h_StationConfigDialog != NULL)
				ComboBox_SetCurSel(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_ITU), ITUIndex);
		}
		else
		{
			ITUIndex = tempRB;
			if (h_StationConfigDialog != NULL)
				ComboBox_SetCurSel(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_ITU), ITUIndex);
		}

		tempRB = 0;
		error = RegQueryValueEx(Settingskey, "LOAuto", NULL, NULL, (LPBYTE)&tempRB, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved LOAuto state");
#endif
			LOplanAuto = true;
		}
		else
		{
			if (tempRB == 1)
				LOplanAuto = true;
			else
				LOplanAuto = false;
		}

		tempRB = 0;
		error = RegQueryValueEx(Settingskey, "PostDCComp", NULL, NULL, (LPBYTE)&tempRB, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved Post DC Compensation state");
#endif
			PostTunerDcCompensation = true;
		}
		else
		{
			if (tempRB == 1)
				PostTunerDcCompensation = true;
			else
				PostTunerDcCompensation = false;
		}

		tempRB = 0;
		error = RegQueryValueEx(Settingskey, "LNAEnabled", NULL, NULL, (LPBYTE)&tempRB, &IntSz);
		if (error != ERROR_SUCCESS)
		{
#ifdef DEBUG_ENABLE
			OutputDebugString("Failed to recall saved LNAEnabled state");
#endif
			LNAEnable = true;
		}
		else
		{
			if (tempRB == 1)
				LNAEnable = true;
			else
				LNAEnable = false;
		}
	}
	else
	{
		//If cannot find any registry settings loads some appropriate defaults
		DcCompensationMode = 3;
		TrackingPeriod = 20;
		RefreshPeriodMemory = 2;
		Frequency = 98.8;
		LOplan = LO120MHz;
		GainReduction = 50;
		AGCsetpoint = -30;
		SampleRateIdx = 0;
		DecimationIdx = 0;
		BandwidthIdx = 3;
		IFmodeIdx = 0;
		FqOffsetPPM = 0;
		AGCEnabled = mir_sdr_AGC_5HZ;
		LOplanAuto = true;
		PostTunerDcCompensation = true;
		LNAEnable = true;
		stationLookup = false;
		workOffline = false;
		ITUIndex = 0;
		TACIndex = 0;
	}

	RegCloseKey(Settingskey);
}

static INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	int SrCount = 0;
	double SR = 0;
	double BW = 0;
	static HWND hGain;
	static HBRUSH BRUSH_RED = CreateSolidBrush(RGB(255, 0, 0));
	static HBRUSH BRUSH_GREEN = CreateSolidBrush(RGB(0, 255, 0));
	static HBRUSH hBrush = CreateSolidBrush(RGB(255, 255, 255));
	DWORD CtrlID;
	UINT nFrom;
//	OPENFILENAME ofn = { 0 };
//	TCHAR szFileName[MAX_PATH] = _T("");
	TCHAR str[255];
	int FreqBand = 0;
	bool DeltaPosClick = false;
	int i;
	int locAGC = 1;

	switch (uMsg)
	{
		case WM_HOTKEY:
			switch (LOWORD(wParam))
			{
			case 1:
				// ALT-A = AGC toggle
				if (AGCEnabled == mir_sdr_AGC_DISABLE)
				{
					AGCEnabled = mir_sdr_AGC_5HZ;
					SendMessage(GetDlgItem(hwndDlg, IDC_RSPAGC), BM_SETCHECK, (WPARAM)(1), 0);
					Edit_Enable(GetDlgItem(hwndDlg, IDC_GR), 0);
					Edit_Enable(GetDlgItem(hwndDlg, IDC_GAINSLIDER), 0);
				}
				else
				{
					AGCEnabled = mir_sdr_AGC_DISABLE;
					SendMessage(GetDlgItem(hwndDlg, IDC_RSPAGC), BM_SETCHECK, (WPARAM)(0), 0);
					Edit_Enable(GetDlgItem(hwndDlg, IDC_GR), 1);
					Edit_Enable(GetDlgItem(hwndDlg, IDC_GAINSLIDER), 1);
				}
				break;

			case 2:
			case 4:
				// - = Increase Gain Reduction
				if (AGCEnabled == mir_sdr_AGC_DISABLE)
				{
					GainReduction += 1;
					if (GainReduction >= 78)
					{
						GainReduction = 78;
					}
					sprintf_s(GainReductionTxt, 254, "%d", GainReduction);
					Edit_SetText(GetDlgItem(hwndDlg, IDC_GR), GainReductionTxt);
					SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETPOS, (WPARAM)true, (LPARAM)GainReduction);
				}
				break;

			case 3:
			case 5:
				// + = Decrease Gain Reduction
				if (AGCEnabled == mir_sdr_AGC_DISABLE)
				{
					GainReduction -= 1;
					if (GainReduction <= 19)
					{
						GainReduction = 19;
					}
					sprintf_s(GainReductionTxt, 254, "%d", GainReduction);
					Edit_SetText(GetDlgItem(hwndDlg, IDC_GR), GainReductionTxt);
					SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETPOS, (WPARAM)true, (LPARAM)GainReduction);
				}
				break;

			case 6:
				// SHIFT+F1 = load profile 1
				if (_tcscmp(p1fname, "") != 0)
				{
					LoadProfile(1);
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "[P1]");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
				}
				break;

			case 7:
				// SHIFT+F2 = load profile 2
				if (_tcscmp(p2fname, "") != 0)
				{
					LoadProfile(2);
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "[P2]");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
				}
				break;

			case 8:
				// SHIFT+F3 = load profile 3
				if (_tcscmp(p3fname, "") != 0)
				{
					LoadProfile(3);
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "[P3]");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
				}
				break;

			case 9:
				// SHIFT+F4 = load profile 4
				if (_tcscmp(p4fname, "") != 0)
				{
					LoadProfile(4);
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "[P4]");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
				}
				break;

			case 10:
				// SHIFT+F5 = load profile 5
				if (_tcscmp(p5fname, "") != 0)
				{
					LoadProfile(5);
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "[P5]");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
				}
				break;

			case 11:
				// SHIFT+F6 = load profile 6
				if (_tcscmp(p6fname, "") != 0)
				{
					LoadProfile(6);
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "[P6]");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
				}
				break;

			case 12:
				// SHIFT+F7 = load profile 7
				if (_tcscmp(p7fname, "") != 0)
				{
					LoadProfile(7);
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "[P7]");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
				}
				break;

			case 13:
				// SHIFT+F8 = load profile 8
				if (_tcscmp(p8fname, "") != 0)
				{
					LoadProfile(8);
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
					Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "[P8]");
				}
				break;

			case 14:
				// ALT-P = toggle Profile panel view
				if (IsWindowVisible(h_ProfilesDialog))
				{
					ShowWindow(h_ProfilesDialog, SW_HIDE);
				}
				else
				{
					ShowWindow(h_ProfilesDialog, SW_SHOW);
				}
				break;

			case 15:
				// ALT-X = toggle EXTIO panel view
				if (IsWindowVisible(h_dialog))
				{
					for (i = 1; i <= (NUM_OF_HOTKEYS - 1); i++) // don't remove ALT-X
					{
						UnregisterHotKey(h_dialog, i);
					}
					definedHotkeys = false;
					ShowWindow(h_dialog, SW_HIDE);
					if (h_AdvancedDialog != NULL)
						ShowWindow(h_AdvancedDialog, SW_HIDE);
					if (h_ProfilesDialog != NULL)
						ShowWindow(h_ProfilesDialog, SW_HIDE);
					if (h_HelpDialog != NULL)
						ShowWindow(h_HelpDialog, SW_HIDE);
				}
				else
				{
					NUM_OF_HOTKEYS = 15;
					RegisterHotKey(h_dialog, 1, MOD_ALT, 0x41);			// ALT-A for AGC enable toggle
					RegisterHotKey(h_dialog, 2, NULL, 0x6D);			// - for Gain decrease (Gain reduction increase) (KEYPAD)
					RegisterHotKey(h_dialog, 3, NULL, 0x6B);			// + for Gain increase (Gain reduction decrease) (KEYPAD)
					RegisterHotKey(h_dialog, 4, NULL, 0xBD);			// - for Gain decrease (Gain reduction increase)
					RegisterHotKey(h_dialog, 5, MOD_SHIFT, 0xBB);		// SHIFT-+ for Gain increase (Gain reduction decrease)
					RegisterHotKey(h_dialog, 6, MOD_SHIFT, 0x70);		// SHIFT+F1 for loading stored profile 1
					RegisterHotKey(h_dialog, 7, MOD_SHIFT, 0x71);		// SHIFT+F2 for loading stored profile 2
					RegisterHotKey(h_dialog, 8, MOD_SHIFT, 0x72);		// SHIFT+F3 for loading stored profile 3
					RegisterHotKey(h_dialog, 9, MOD_SHIFT, 0x73);		// SHIFT+F4 for loading stored profile 4
					RegisterHotKey(h_dialog, 10, MOD_SHIFT, 0x74);		// SHIFT+F5 for loading stored profile 5
					RegisterHotKey(h_dialog, 11, MOD_SHIFT, 0x75);		// SHIFT+F6 for loading stored profile 6
					RegisterHotKey(h_dialog, 12, MOD_SHIFT, 0x76);		// SHIFT+F7 for loading stored profile 7
					RegisterHotKey(h_dialog, 13, MOD_SHIFT, 0x77);		// SHIFT+F8 for loading stored profile 8
					RegisterHotKey(h_dialog, 14, MOD_ALT, 0x50);		// ALT-P for Profile panel view toggle
					RegisterHotKey(h_dialog, 15, MOD_ALT, 0x58);		// ALT-X for EXTIO panel view toggle
					definedHotkeys = true;
					ShowWindow(h_dialog, SW_SHOW);
				}
				return true;
				break;
			}
			break;

		case WM_TIMER:
			_stprintf_s(str, 255, TEXT("%d"), GainReduction);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_GR), 1);
			SendMessage(GetDlgItem(hwndDlg, IDC_GR), WM_SETTEXT, (WPARAM)0, (LPARAM)str);				
			Edit_Enable(GetDlgItem(hwndDlg, IDC_GR), 0);

			Edit_Enable(GetDlgItem(hwndDlg, IDC_GAINSLIDER), 1);
			SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETPOS, (WPARAM)true, (LPARAM)GainReduction);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_GAINSLIDER), 0);

         if (LNAEnable)
         {
            Edit_SetText(GetDlgItem(hwndDlg, IDC_LNAGR), "LNA GR 0 dB");
            Edit_SetText(GetDlgItem(hwndDlg, IDC_LNASTATE), "LNA ON");
            //SendMessage(GetDlgItem(hwndDlg, IDC_LNAGR_SWITCH), BM_SETCHECK, BST_CHECKED, 0);
         }
         else
         {
            sprintf_s(str, sizeof(str), "%s %d %s", "LNA GR", LnaGr, "dB");
            Edit_SetText(GetDlgItem(hwndDlg, IDC_LNAGR), str);
            Edit_SetText(GetDlgItem(hwndDlg, IDC_LNASTATE), "LNA OFF");
            //SendMessage(GetDlgItem(hwndDlg, IDC_LNAGR_SWITCH), BM_SETCHECK, BST_UNCHECKED, 0);
         }

		sprintf_s(str, sizeof(str), "IF Gain Reduction %d dB", GainReduction);
		Edit_SetText(GetDlgItem(hwndDlg, IDC_IFGR), str);
		sprintf_s(str, sizeof(str), "Total System Gain Reduction %d dB", SystemGainReduction);
		Edit_SetText(GetDlgItem(hwndDlg, IDC_TOTALGR), str);
		break;

		case WM_NOTIFY:
			nFrom = ((LPNMHDR)lParam)->code;
			switch (nFrom)
			{
			case UDN_DELTAPOS:
			{
				if (((LPNMHDR)lParam)->idFrom == IDC_PPM_S)
				{
					TCHAR Offset[255];
					LPNMUPDOWN lpnmud = (LPNMUPDOWN)lParam;

					DeltaPosClick = true;
					FqOffsetPPM = FqOffsetPPM - ((float)lpnmud->iDelta / 100);
					sprintf_s(Offset, 255, "%.2f", FqOffsetPPM);
					Edit_SetText(GetDlgItem(hwndDlg, IDC_PPM), Offset);
#ifdef DEBUG_ENABLE
					sprintf_s(str, 255, "PPM adjust: %.2f", FqOffsetPPM);
					OutputDebugString(str);
#endif
					return false;
				}
			}
		}
		break;
		
		case WM_SYSCOMMAND:
			switch (wParam & 0xFFF0)
			{
				case SC_SIZE:
				case SC_MINIMIZE:
				case SC_MAXIMIZE:
				return true;
			}
			break;

		case WM_CLOSE:
			for (i = 1; i <= NUM_OF_HOTKEYS; i++)
			{
				UnregisterHotKey(h_dialog, i);
			}
			definedHotkeys = false;
			ShowWindow(h_dialog, SW_HIDE);
			if (h_AdvancedDialog != NULL)
				ShowWindow(h_AdvancedDialog, SW_HIDE);
			if (h_ProfilesDialog != NULL)
				ShowWindow(h_ProfilesDialog, SW_HIDE);
			if (h_HelpDialog != NULL)
				ShowWindow(h_HelpDialog, SW_HIDE);
			if (h_StationConfigDialog != NULL)
				ShowWindow(h_StationConfigDialog, SW_HIDE);
			return true;
			break;

		case WM_DESTROY:
			for (i = 1; i <= NUM_OF_HOTKEYS; i++)
			{
				UnregisterHotKey(h_dialog, i);
			}
			definedHotkeys = false;
			h_dialog = NULL;
			return true;
			break;

		case WM_VSCROLL:
			if ((HWND)lParam == GetDlgItem(hwndDlg, IDC_GAINSLIDER))
			{
#ifdef DEBUG_ENABLE
				OutputDebugString("Gain Slider VM Scroll");
#endif
				int pos = SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_GETPOS, (WPARAM)0, (LPARAM)0);
				GainReduction = pos;
				sprintf_s(GainReductionTxt, 254, "%d", pos);
				Edit_SetText(GetDlgItem(hwndDlg, IDC_GR), GainReductionTxt);
//				ProgramGr((int *)&GainReduction, &SystemGainReduction, LNAGainReduction, 1);
				profileChanged = true;
			}
			break;
			
		case WM_INITDIALOG:
			// Buffer size

			ghwndDlg = hwndDlg;
			TCHAR SampleRateTxt[255];
			TCHAR DecimationTxt[255];

			LoadSettings();
			//Buffer Size (currently not used)
			for (i = 0; i < (sizeof(buffer_sizes) / sizeof(buffer_sizes[0])); i++)  //Work out the size of the buffer.
			{
				_stprintf_s(str, 255, TEXT("%d kB"), buffer_sizes[i]);
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_BUFFER), str);
			}
			ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_BUFFER), buffer_default);
			buffer_len_samples = buffer_sizes[buffer_default] * 1024;

			//Add IF mode strings and select stored IF mode
			ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_IFMODE));
			_stprintf_s(str, 255, "Zero IF");
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFMODE), str);
			_stprintf_s(str, 255, "Low IF");
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFMODE), str);
			ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_IFMODE), IFmodeIdx);

			//Add Filter BW based on IF mode Selection then select stored BW.
			if (IFmodeIdx == 0)
			{
				IFMode = mir_sdr_IF_Zero;
				ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_IFBW));
				_stprintf_s(str, 255, "200 kHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				_stprintf_s(str, 255, "300 kHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				_stprintf_s(str, 255, "600 kHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				_stprintf_s(str, 255, "1.536 MHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				_stprintf_s(str, 255, "5.000 MHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				_stprintf_s(str, 255, "6.000 MHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				_stprintf_s(str, 255, "7.000 MHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				_stprintf_s(str, 255, "8.000 MHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_IFBW), BandwidthIdx);
			}
			else
			{
				ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_IFBW));
				_stprintf_s(str, 255, "200 kHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				_stprintf_s(str, 255, "300 kHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				_stprintf_s(str, 255, "600 kHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				_stprintf_s(str, 255, "1.536 MHz");
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
				ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_IFBW), BandwidthIdx);
			}	
//			BandwidthIdx = ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_IFBW), BandwidthIdx);
			Bandwidth = bandwidths[BandwidthIdx].bwType;

			//Add Samplerates based Filter BW  & IF mode then select stored Samplerate.
			if (IFmodeIdx == 1)
			{
				//Disable Sample Rate Display & Decimation Dialog Boxes
				Edit_Enable(GetDlgItem(hwndDlg, IDC_SAMPLERATE), 0);
				Edit_Enable(GetDlgItem(hwndDlg, IDC_DECIMATION), 0);

				//LIF Mode
				if (Bandwidth == mir_sdr_BW_1_536)
				{
					IFMode = mir_sdr_IF_2_048;
					ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_SAMPLERATE));
					_stprintf_s(str, 255, TEXT("%.2f"), 8.192);
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_SAMPLERATE), str);
					ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), 0);
				}
					if (Bandwidth == mir_sdr_BW_0_200 || Bandwidth == mir_sdr_BW_0_300 || Bandwidth == mir_sdr_BW_0_600)
				{
					IFMode = mir_sdr_IF_0_450;
					ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_SAMPLERATE));
					_stprintf_s(str, 255, TEXT("%.2f"), 2.0);
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_SAMPLERATE), str);
					ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), 0);
				}
			}
			else
			{
				//Enable Sample Rate Display & Decimation Dialog Boxes
				Edit_Enable(GetDlgItem(hwndDlg, IDC_SAMPLERATE), 1);
				Edit_Enable(GetDlgItem(hwndDlg, IDC_DECIMATION), 1);

				//ZIF Mode
				SrCount = (sizeof(samplerates) / sizeof(samplerates[0]));

				//calculate minimum SampleRate for stored BW & IF Settings
				BW = (float)bandwidths[BandwidthIdx].BW;

				//Add Sample rates to dialog box based on calculated Minimum & identify chosen rate on way through.
				int target = 0;
				ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_SAMPLERATE));
				for (i = 0; i < SrCount; i++)
				{
					SR = samplerates[i].value + 0.06;
					if (SR > BW)   //BW = minimum sample rate.
					{
						_stprintf_s(str, 255, TEXT("%.2f"), samplerates[i].value);
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_SAMPLERATE), str);
						if (samplerates[i].value == samplerates[SampleRateIdx].value)
							target = i;
					}
					ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), target);
				}
			}

			//Populate Decimate Control With Appropriate Sample rates
			ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_DECIMATION));
			_stprintf_s(str, 255, "None");
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);

			for (i = 1; i < (sizeof(Decimation) / sizeof(Decimation[0])); i++)
				if ((samplerates[SampleRateIdx].value / Decimation[i].DecValue) >= (float)bandwidths[BandwidthIdx].BW)
				{
#ifdef DEBUG_ENABLE
					_stprintf_s(str, 255, "decimation Values permitted: %d, SR: %f, BW: %f", Decimation[i].DecValue, samplerates[SampleRateIdx].value, (float)bandwidths[BandwidthIdx].BW);
					OutputDebugString(str);
#endif
					_stprintf_s(str, 255, TEXT("%d"), Decimation[i].DecValue);
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
				}
			if (IFmodeIdx == 0)
			{
				ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_DECIMATION), DecimationIdx);
			}
			else
			{
				if (Bandwidth == mir_sdr_BW_0_600)
					DecimationIdx = 1;
				else
					DecimationIdx = 2;

				ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_DECIMATION), DecimationIdx);
			}

			DecimateEnable = 0;
			if (DecimationIdx > 0 && (IFmodeIdx == 0))
				DecimateEnable = 1;

			WinradCallBack(-1, WINRAD_SRCHANGE, 0, NULL);

			// LNA Enable
			if (LNAEnable)
			{
				SendMessage(GetDlgItem(hwndDlg, IDC_LNAGR_SWITCH), BM_SETCHECK, BST_CHECKED, 0);
			}
			else
			{
				SendMessage(GetDlgItem(hwndDlg, IDC_LNAGR_SWITCH), BM_SETCHECK, BST_UNCHECKED, 0);
			}

			//// Decimation
			//ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_DECIMATION));
			//_stprintf_s(str, 255, "None");
			//ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
			//_stprintf_s(str, 255, "2");
			//ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
			//_stprintf_s(str, 255, "4");
			//ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
			//_stprintf_s(str, 255, "8");
			//ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
			//_stprintf_s(str, 255, "16");
			//ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
			//ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_DECIMATION), DecimationIdx);

			//Gain Reduction
			sprintf_s(GainReductionTxt, 254, "%d", GainReduction);
			SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETRANGEMIN, (WPARAM)true, (LPARAM)19);
			SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETRANGEMAX, (WPARAM)true, (LPARAM)78);
			SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETPOS, (WPARAM)true, (LPARAM)GainReduction);
			Edit_SetText(GetDlgItem(hwndDlg, IDC_GR), GainReductionTxt);

			//ADC Setpoint
			sprintf_s(AGCsetpointTxt, 254, "%d", AGCsetpoint);
			SendMessage(GetDlgItem(hwndDlg, IDC_SETPOINT_S), UDM_SETRANGE, (WPARAM)true, 0 | (-50 << 16));
			Edit_SetText(GetDlgItem(hwndDlg, IDC_SETPOINT), AGCsetpointTxt);

			//Frequency Offset
			sprintf_s(FreqoffsetTxt, 254, "%.2f", FqOffsetPPM);
			SendMessage(GetDlgItem(hwndDlg, IDC_PPM_S), UDM_SETRANGE, (WPARAM)0, (LPARAM)(100000 & 0xFFFF) | (((LPARAM)(-100000) << 16) & 0xFFFF0000));
			Edit_SetText(GetDlgItem(hwndDlg, IDC_PPM), FreqoffsetTxt);

			//AGC Enabled
			if (AGCEnabled == mir_sdr_AGC_DISABLE)
				locAGC = 0;
			SendMessage(GetDlgItem(hwndDlg, IDC_RSPAGC), BM_SETCHECK, (WPARAM)(locAGC ? BST_CHECKED : BST_UNCHECKED), (LPARAM)0);
			SendMessage(GetDlgItem(hwndDlg, IDC_GR), WM_ENABLE, (WPARAM)(locAGC ? 0 : 1), 1);

			FreqBand = 0;
			while (Frequency > band_fmin[FreqBand + 1] && FreqBand + 1 < NUM_BANDS)
				FreqBand++;

			sprintf_s(str, sizeof(str), "IF Gain Reduction %d dB", GainReduction);
			Edit_SetText(GetDlgItem(hwndDlg, IDC_IFGR), str);
			if (LNAEnable)
			{
				sprintf_s(str, sizeof(str), "Total System Gain Reduction %d dB", GainReduction);
				Edit_SetText(GetDlgItem(hwndDlg, IDC_TOTALGR), str);
			}
			else
			{
				sprintf_s(str, sizeof(str), "Total System Gain Reduction %d dB", GainReduction + band_LNAgain[FreqBand]);
				Edit_SetText(GetDlgItem(hwndDlg, IDC_TOTALGR), str);
			}

			if (h_AdvancedDialog == NULL)
			{
				h_AdvancedDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_ADVANCED), NULL, (DLGPROC)AdvancedDlgProc);  //(DLGPROC)AdvancedDlgProc
//				ShowWindow(h_AdvancedDialog, SW_SHOW);
				ShowWindow(h_AdvancedDialog, SW_HIDE);
			}

			if (h_ProfilesDialog == NULL)
			{
				h_ProfilesDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_PROFILES_PANEL), NULL, (DLGPROC)ProfilesDlgProc);  //(DLGPROC)ProfilesDlgProc
//				ShowWindow(h_ProfilesDialog, SW_SHOW);
				ShowWindow(h_ProfilesDialog, SW_HIDE);
			}

			if (h_HelpDialog == NULL)
			{
				h_HelpDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_HELP_PANEL), NULL, (DLGPROC)HelpDlgProc);
				ShowWindow(h_HelpDialog, SW_HIDE);
			}

			if (h_StationDialog == NULL)
			{
				h_StationDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_STATION_PANEL), NULL, (DLGPROC)StationDlgProc);
				ShowWindow(h_StationDialog, SW_HIDE);
			}

			if (h_StationConfigDialog == NULL)
			{
				h_StationConfigDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_STATIONCONFIG_PANEL), NULL, (DLGPROC)StationConfigDlgProc);
				ShowWindow(h_StationConfigDialog, SW_HIDE);
			}

			return true;
			break;

		case WM_CTLCOLORSTATIC:
			CtrlID = GetDlgCtrlID((HWND)lParam);
			switch (CtrlID)
			{
			//case (IDC_STATIC1) :
			case (IDC_IFGR):
			case (IDC_TOTALGR):
			case (IDC_LNAGR):
			case (IDC_LNASTATE):
			case (IDC_LNAGR_SWITCH):
			case (IDC_FINALSR):
			case (IDC_OVERLOAD) :
					HDC hdcStatic = (HDC)wParam;
					SetBkColor(hdcStatic, RGB(255, 255, 255));
					if (CtrlID == IDC_OVERLOAD)
					{
						SetTextColor(hdcStatic, RGB(255, 0, 0));
					}
					return (INT_PTR)hBrush;
					break;
			}
			break;

		case WM_COMMAND:
			switch (GET_WM_COMMAND_ID(wParam, lParam))
			{
			case IDC_Advanced:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
				{
					if (h_AdvancedDialog == NULL)
						h_AdvancedDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_ADVANCED), NULL, (DLGPROC)AdvancedDlgProc);
					ShowWindow(h_AdvancedDialog, SW_SHOW);
					SetForegroundWindow(h_AdvancedDialog);
					return true;
				}
				break;

			case IDC_StationName_PopOut:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
				{
					if (h_StationDialog == NULL)
						h_StationDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_STATION_PANEL), NULL, (DLGPROC)StationDlgProc);
					ShowWindow(h_StationDialog, SW_SHOW);
					SetForegroundWindow(h_StationDialog);
					return true;
				}
				break;

			case IDC_ProfilesButton:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
				{
					if (h_ProfilesDialog == NULL)
						h_ProfilesDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_PROFILES_PANEL), NULL, (DLGPROC)ProfilesDlgProc);
					ShowWindow(h_ProfilesDialog, SW_SHOW);
					SetForegroundWindow(h_ProfilesDialog);
					return true;
				}
				break;

			case IDC_HELP_BUTTON:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
				{
					if (h_HelpDialog == NULL)
						h_HelpDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_HELP_PANEL), NULL, (DLGPROC)HelpDlgProc);
					ShowWindow(h_HelpDialog, SW_SHOW);
					SetForegroundWindow(h_HelpDialog);
					return true;
				}
				break;
			
			case IDC_SAMPLERATE:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
				{
					double WantedSR;
//					float a;

					Edit_GetText(GetDlgItem(hwndDlg, IDC_SAMPLERATE), SampleRateTxt, 255);
					WantedSR = atof(SampleRateTxt);

#ifdef DEBUG_ENABLE
					sprintf_s(str, 255, "WantedSR: %f", WantedSR);
					OutputDebugString(str);
#endif

					SampleRateIdx = FindSampleRateIdx(WantedSR);

#ifdef DEBUG_ENABLE
					sprintf_s(str, 255, "SampleRateIdx: %d", SampleRateIdx);
					OutputDebugString(str);
#endif

					mir_sdr_DecimateControl_fn(0, 0, 0);
					DecimationIdx = 0;
					DecimateEnable = 0;

					//Populate Decimate Control With Appropriate Sample rates
					ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_DECIMATION));
					_stprintf_s(str, 255, "None");
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);

					for (i = 1; i < (sizeof(Decimation) / sizeof(Decimation[0])); i++)
						if ((samplerates[SampleRateIdx].value / Decimation[i].DecValue) >= (float)bandwidths[BandwidthIdx].BW)
						{
#ifdef DEBUG_ENABLE
							_stprintf_s(str, 255, "decimation Values permitted: %d, SR: %f, BW: %f", Decimation[i].DecValue, samplerates[SampleRateIdx].value, (float)bandwidths[BandwidthIdx].BW);
							OutputDebugString(str);
#endif
							_stprintf_s(str, 255, TEXT("%d"), Decimation[i].DecValue);
							ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
						}

					ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_DECIMATION), DecimationIdx);
					if (Running)
					{
						mir_sdr_Reinit_fn(pZERO, samplerates[SampleRateIdx].value, 0.0, mir_sdr_BW_Undefined, mir_sdr_IF_Undefined, mir_sdr_LO_Undefined, 0, pZERO, mir_sdr_USE_SET_GR_ALT_MODE, pZERO, (mir_sdr_ReasonForReinitT)(mir_sdr_CHANGE_FS_FREQ));
					}
					WinradCallBack(-1, WINRAD_SRCHANGE, 0, NULL);
					return true;
				}
				break;

			case IDC_DECIMATION:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
				{
#ifdef DEBUG_ENABLE
					OutputDebugString("IDC_DECIMATION Changed");
#endif
//					double WantedSR;
					int decValue;

					DecimationIdx = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));
					SendMessage(GetDlgItem(hwndDlg, IDC_DECIMATION), CB_GETLBTEXT, DecimationIdx, (LPARAM)DecimationTxt);
					decValue = atoi(DecimationTxt);

					if (DecimationIdx == 0)
					{
#ifdef DEBUG_ENABLE
						OutputDebugString("Decimation disabled");
#endif
						DecimateEnable = 0;
						mir_sdr_DecimateControl_fn(DecimateEnable, 0, 0);
						WinradCallBack(-1, WINRAD_SRCHANGE, 0, NULL);
						return true;
					}
					else
					{
#ifdef DEBUG_ENABLE
						OutputDebugString("Decimation Enabled");
#endif
						DecimateEnable = 1;
						mir_sdr_DecimateControl_fn(DecimateEnable, decValue, 0);
						//SampleRateIdx = FindSampleRateIdx(WantedSR);
						WinradCallBack(-1, WINRAD_SRCHANGE, 0, NULL);
						return true;
					}
				}
				break;
			
			case IDC_BUFFER:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
				{
					buffer_default = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));
					if (Running)
					{
						//StopHW();
						buffer_len_samples = buffer_sizes[buffer_default] * 1024;
						WinradCallBack(-1, WINRAD_SRCHANGE, 0, NULL);
						::Sleep(200);
					}
					else
						buffer_len_samples = buffer_sizes[buffer_default] * 1024;
					return true;
				}
				break;

			case IDC_RSPAGC:
				if (ghwndDlg)	// update only when already initialized!
				{
					if (GET_WM_COMMAND_CMD(wParam, lParam) == BST_UNCHECKED)
					{
						if (Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED) //it is checked
						{
							AGCEnabled = mir_sdr_AGC_5HZ;
							Edit_Enable(GetDlgItem(hwndDlg, IDC_GR), 0);
							Edit_Enable(GetDlgItem(hwndDlg, IDC_GAINSLIDER), 0);
						}
						else
						{
							AGCEnabled = mir_sdr_AGC_DISABLE;
							Edit_Enable(GetDlgItem(hwndDlg, IDC_GR), 1);
							Edit_Enable(GetDlgItem(hwndDlg, IDC_GAINSLIDER), 1);
						}
						if (Running)
						{
							mir_sdr_AgcControl_fn(AGCEnabled, AGCsetpoint, 0, 0, 0, 0, LNAEnable);
						}
					}
					return true;
				}
				break;

			case IDC_SETPOINT:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_UPDATE)
				{
					TCHAR SP[255];
					// need to have error checking applied here.
					Edit_GetText((HWND)lParam, SP, 255);
					if (ghwndDlg)	// update only when already initialized!
						AGCsetpoint = _ttoi(SP);
					if (Running)
						mir_sdr_AgcControl_fn(AGCEnabled, AGCsetpoint, 0, 0, 0, 0, LNAEnable);
					return true;
				}
				break;
			
			case IDC_PPM:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_UPDATE)
				{
					TCHAR Freqoffset[255];
					if (!DeltaPosClick)
						Edit_GetText(GetDlgItem(hwndDlg, IDC_PPM), Freqoffset, 255);
					FqOffsetPPM = (float)atof(Freqoffset);

					DeltaPosClick = false;
					if (Running)
					{
#ifdef DEBUG_ENABLE
						OutputDebugString("Call PPM Function");
#endif
						mir_sdr_SetPpm_fn((double)FqOffsetPPM);
					}
					return true;
				}
				break;
			
			case IDC_GR:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_UPDATE)
				{
					TCHAR GR[255];
					if (AGCEnabled == mir_sdr_AGC_DISABLE)
					{
						Edit_GetText((HWND)lParam, GR, 255);
						if (ghwndDlg)	// update only when already initialized!
							GainReduction = _ttoi(GR);

                  if (Running)
                  {
                     ProgramGr((int *)&GainReduction, &SystemGainReduction, (int)LNAEnable, 1);
                  }
                  else
                  {
                     mir_sdr_GetGrByFreq_fn(Frequency, (mir_sdr_BandT *)&FreqBand, (int *)&GainReduction, LNAEnable, &SystemGainReduction, mir_sdr_USE_SET_GR_ALT_MODE);
                  }

						SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETPOS, (WPARAM)true, (LPARAM)GainReduction);

                  sprintf_s(str, sizeof(str), "IF Gain Reduction %d dB", GainReduction);
						Edit_SetText(GetDlgItem(hwndDlg, IDC_IFGR), str);
                  sprintf_s(str, sizeof(str), "Total System Gain Reduction %d dB", SystemGainReduction);
						Edit_SetText(GetDlgItem(hwndDlg, IDC_TOTALGR), str);

						return true;
					}
               else
               {
						return true;
					}
				}
				return true;

			case IDC_EXIT:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
				{
					for (i = 1; i <= NUM_OF_HOTKEYS; i++)
					{
						UnregisterHotKey(h_dialog, i);
					}
					definedHotkeys = false;
					ShowWindow(h_dialog, SW_HIDE);
					if (h_AdvancedDialog != NULL)
						ShowWindow(h_AdvancedDialog, SW_HIDE);
					if (h_ProfilesDialog != NULL)
						ShowWindow(h_ProfilesDialog, SW_HIDE);
					if (h_HelpDialog != NULL)
						ShowWindow(h_HelpDialog, SW_HIDE);
					return true;
				}
				break;

			case IDC_LoadDefaults:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
				{
					_stprintf_s(str, TEXT("Current Profile: Default"));
					Edit_SetText(GetDlgItem(hwndDlg, IDC_ProfileFileName), str);

					//IF Mode
					IFMode = mir_sdr_IF_Zero;
					IFmodeIdx = 0;
					ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_IFMODE), IFmodeIdx);

					//Bandwidth
					ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_IFBW));
					_stprintf_s(str, 255, "200 kHz");
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
					_stprintf_s(str, 255, "300 kHz");
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
					_stprintf_s(str, 255, "600 kHz");
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
					_stprintf_s(str, 255, "1.536 MHz");
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
					_stprintf_s(str, 255, "5.000 MHz");
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
					_stprintf_s(str, 255, "6.000 MHz");
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
					_stprintf_s(str, 255, "7.000 MHz");
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
					_stprintf_s(str, 255, "8.000 MHz");
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
					BandwidthIdx = 3;
					BandwidthIdx = ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_IFBW), BandwidthIdx);
					Bandwidth = bandwidths[BandwidthIdx].bwType;

					//Samplerate
					SampleRateIdx = 0;
					//SampleRate = samplerates[SampleRateIdx].value;
					ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_SAMPLERATE));
					for (i = 0; i < (sizeof(samplerates) / sizeof(samplerates[0])); i++)
					{
						SR = samplerates[i].value;
						_stprintf_s(str, 255, TEXT("%.2f"), SR);
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_SAMPLERATE), str);
					}
					ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), SampleRateIdx);
					SR = 2.0;

					//Gain Reduction
					GainReduction = 50;
					sprintf_s(GainReductionTxt, 254, "%d", GainReduction);
					Edit_SetText(GetDlgItem(hwndDlg, IDC_GR), GainReductionTxt);
					SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETPOS, (WPARAM)true, (LPARAM)GainReduction);
					//AGCSetpoint
					AGCsetpoint = -30;
					sprintf_s(AGCsetpointTxt, 254, "%d", AGCsetpoint);
					Edit_SetText(GetDlgItem(hwndDlg, IDC_SETPOINT), AGCsetpointTxt);
					//Frequency Offset
					FqOffsetPPM = 0;
					sprintf_s(FreqoffsetTxt, 254, "%.2f", FqOffsetPPM);
					Edit_SetText(GetDlgItem(hwndDlg, IDC_PPM), FreqoffsetTxt);
					//AGC
					AGCEnabled = mir_sdr_AGC_5HZ;
					SendMessage(GetDlgItem(hwndDlg, IDC_RSPAGC), BM_SETCHECK, (WPARAM)(1), 0);
					Edit_Enable(GetDlgItem(hwndDlg, IDC_GR), 0);
					Edit_Enable(GetDlgItem(hwndDlg, IDC_GAINSLIDER), 0);

					//LOPLan
					LOplan = LO120MHz;
					LOplanAuto = true;
					WinradCallBack(-1, WINRAD_SRCHANGE, 0, NULL);

					DcCompensationMode = PERIODIC3;
					TrackingPeriod = 32;
					RefreshPeriodMemory = 2;
					PostTunerDcCompensation = true;

					// LNA Enable
					//SendMessage(GetDlgItem(hwndDlg, IDC_LNAGR_SWITCH), BM_SETCHECK, BST_CHECKED, 0);
					Edit_SetText(GetDlgItem(hwndDlg, IDC_LNAGR), "LNA GR 0 dB");
					Edit_SetText(GetDlgItem(hwndDlg, IDC_LNASTATE), "LNA ON");
					LNAEnable = true;
					sprintf_s(str, sizeof(str), "Total System Gain Reduction %d dB", GainReduction);
					Edit_SetText(GetDlgItem(hwndDlg, IDC_TOTALGR), str);

					if (Running)
					{
						ReinitAll();
					}

					// Station Lookup
					stationLookup = false;
					workOffline = false;
					ITUIndex = 0;
					TACIndex = 0;

					if (h_StationConfigDialog != NULL)
					{
						SendMessage(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_ENABLE), BM_SETCHECK, (WPARAM)(0), 0);
						SendMessage(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_OFFLINE), BM_SETCHECK, (WPARAM)(0), 0);
						ComboBox_SetCurSel(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_ITU), ITUIndex);
						ComboBox_SetCurSel(GetDlgItem(h_StationConfigDialog, IDC_STATIONCONFIG_TAC), TACIndex);
					}
					return true;
				}
				break;

			case IDC_IFBW:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
				{
					BandwidthIdx = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));
					Bandwidth = bandwidths[BandwidthIdx].bwType;
					BW = (float)bandwidths[BandwidthIdx].BW;

#ifdef DEBUG_ENABLE
					_stprintf_s(str, 255, "BW: %f", BW);
					OutputDebugString(str);
#endif

					//ZIF Mode selected

					if (IFmodeIdx == 0)
					{
						//Populate Sample Rate Control With Appropriate Sample rates
						ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_SAMPLERATE));
						bool first = true;
						double localSR;
						for (i = 0; i < (sizeof(samplerates) / sizeof(samplerates[0])); i++)
						{
							localSR = samplerates[i].value + 0.06;		//Subtract small value as cannot use == with two doubles.
							if (localSR > BW)
							{
								_stprintf_s(str, 255, TEXT("%.2f"), samplerates[i].value);
								ComboBox_AddString(GetDlgItem(hwndDlg, IDC_SAMPLERATE), str);
								if (first)
								{
									SampleRateIdx = FindSampleRateIdx(atof(str));
									SR = samplerates[SampleRateIdx].value;
									first = false;
#ifdef DEBUG_ENABLE
									OutputDebugString(str);
#endif
								}
							}
						}

#ifdef DEBUG_ENABLE
						sprintf_s(str, 255, "IDC_IFBW: BandwidthIdx: %d", BandwidthIdx);
						OutputDebugString(str);
						sprintf_s(str, 255, "IDC_IFBW: SampleRateIdx: %d", SampleRateIdx);
						OutputDebugString(str);
#endif

						ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), 0);

						//Populate Decimate Control With Appropriate Sample rates
						ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_DECIMATION));
						_stprintf_s(str, 255, "None");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
						for (i = 1; i < (sizeof(Decimation) / sizeof(Decimation[0])); i++)
						{
							if (((samplerates[SampleRateIdx].value + 0.1) / Decimation[i].DecValue) > BW)
							{
								_stprintf_s(str, 255, TEXT("%d"), Decimation[i].DecValue);
								ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
							}
						}

						if (BW > 1)
						{
							DecimationIdx = 0;
							DecimateEnable = 0;
						}
						else if (BW > 0.31)
						{
							DecimationIdx = 1;
							DecimateEnable = 1;
						}
						else if (BW > 0.21)
						{
							DecimationIdx = 2;
							DecimateEnable = 1;
						}
						else
						{
							DecimationIdx = 3;
							DecimateEnable = 1;
						}

						ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_DECIMATION), DecimationIdx);
						if (DecimateEnable == 1 && DecimationIdx > 0)
							SR = SR / Decimation[DecimationIdx].DecValue;

						//Load defaults from Default Samplerates
						/*
						SampleRateDisplayIdx = 0;
						ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), SampleRateDisplayIdx);
						SendMessage(GetDlgItem(hwndDlg, IDC_SAMPLERATE), CB_GETLBTEXT, 0, (LPARAM)SampleRateTxt);	//readback chosen sample rates and work out Idx
						WantedSR = atof(SampleRateTxt);
						SampleRateIdx = FindSampleRateIdx(WantedSR);
						*/
					}
					else
					{
						// Low IF mode
						if (Bandwidth == mir_sdr_BW_1_536)
						{
							IFMode = mir_sdr_IF_2_048;
							ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_SAMPLERATE));
							_stprintf_s(str, 255, TEXT("%.2f"), 8.192);
							ComboBox_AddString(GetDlgItem(hwndDlg, IDC_SAMPLERATE), str);
							ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), 0);
							SampleRateIdx = 6;
							SR = 8.192;
							//Populate Decimate Control With Appropriate Sample rates
							ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_DECIMATION));
							_stprintf_s(str, 255, "None");
							ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
							_stprintf_s(str, 255, "2");
							ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
							_stprintf_s(str, 255, "4");
							ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
							DecimationIdx = 2;
							ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_DECIMATION), DecimationIdx);
							DecimateEnable = 0;
						}
						else
						{
							IFMode = mir_sdr_IF_0_450;
							ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_SAMPLERATE));
							_stprintf_s(str, 255, TEXT("%.2f"), 2.0);
							ComboBox_AddString(GetDlgItem(hwndDlg, IDC_SAMPLERATE), str);
							ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), 0);
							SampleRateIdx = 0;
							SR = 2.0;
							//Populate Decimate Control With Appropriate Sample rates
							ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_DECIMATION));
							_stprintf_s(str, 255, "None");
							ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
							_stprintf_s(str, 255, "2");
							ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
							_stprintf_s(str, 255, "4");
							ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
							if (Bandwidth == mir_sdr_BW_0_600)
								DecimationIdx = 1;
							else
								DecimationIdx = 2;
							ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_DECIMATION), DecimationIdx);
							DecimateEnable = 0;
						}
					}

#ifdef DEBUG_ENABLE
					sprintf_s(str, 255, "SampleRateIdx before callback: %d", SampleRateIdx);
					OutputDebugString(str);
#endif
					WinradCallBack(-1, WINRAD_SRCHANGE, 0, NULL);

					if (Running)
					{
						ReinitAll();
						//mir_sdr_Reinit_fn(pZERO, SR, 0.0, Bandwidth, IFMode, mir_sdr_LO_Undefined, 0, pZERO, 1, pZERO, (mir_sdr_ReasonForReinitT)(mir_sdr_CHANGE_BW_TYPE | mir_sdr_CHANGE_IF_TYPE | mir_sdr_CHANGE_FS_FREQ));
						//mir_sdr_DecimateControl_fn(DecimateEnable, Decimation[DecimationIdx].DecValue, 0);
					}

					return true;
				}
				break;
			
			case IDC_IFMODE:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
				{
					IFmodeIdx = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));
					if (IFmodeIdx == 0)
					{
						//Add Filter Bandwidths to IFBW dialog Box
						ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_IFBW));
						_stprintf_s(str, 255, "200 kHz");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
						_stprintf_s(str, 255, "300 kHz");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
						_stprintf_s(str, 255, "600 kHz");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
						_stprintf_s(str, 255, "1.536 MHz");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
						_stprintf_s(str, 255, "5.000 MHz");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
						_stprintf_s(str, 255, "6.000 MHz");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
						_stprintf_s(str, 255, "7.000 MHz");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
						_stprintf_s(str, 255, "8.000 MHz");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);	

						//Set IF Bandwidth to 200kHz as Default
						BandwidthIdx = 0;
						ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_IFBW), BandwidthIdx);
						Bandwidth = bandwidths[BandwidthIdx].bwType;

						//Set IF Mode to ZIF
						IFMode = mir_sdr_IF_Zero;

						//Set Samplerate for 200kHz filter default
						SampleRateIdx = 0;
						ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_SAMPLERATE));
						for (i = 0; i < (sizeof(samplerates) / sizeof(samplerates[0])); i++)
						{
							SR = samplerates[i].value;
							_stprintf_s(str, 255, TEXT("%.2f"), SR);
							ComboBox_AddString(GetDlgItem(hwndDlg, IDC_SAMPLERATE), str);
						}
						ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), 0);
						SR = 2.0 / 8;

						DecimationIdx = 3;
						DecimateEnable = 1;
						ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_DECIMATION));
						_stprintf_s(str, 255, "None");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
						_stprintf_s(str, 255, "2");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
						_stprintf_s(str, 255, "4");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
						_stprintf_s(str, 255, "8");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
						ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_DECIMATION), DecimationIdx);

						//Enable Sample Rate & Decimation Dialog Boxes
						Edit_Enable(GetDlgItem(hwndDlg, IDC_SAMPLERATE), 1);
						Edit_Enable(GetDlgItem(hwndDlg, IDC_DECIMATION), 1);
					}

					if (IFmodeIdx == 1)
					{
						//Add filter Bandwidths for Low IF modes
						ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_IFBW));
						_stprintf_s(str, 255, "200 kHz");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
						_stprintf_s(str, 255, "300 kHz");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
						_stprintf_s(str, 255, "600 kHz");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);
						_stprintf_s(str, 255, "1.536 MHz");						
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFBW), str);	

						//Set IF Bandwidth to 200kHz as Default
						BandwidthIdx = 0;
						ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_IFBW), BandwidthIdx);
						Bandwidth = bandwidths[BandwidthIdx].bwType;

						//IF Mode
						IFMode = mir_sdr_IF_0_450;

						//Set Samplerate for 200kHz filter default
						SampleRateIdx = 0;
						ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_SAMPLERATE));
						_stprintf_s(str, 255, TEXT("%.2f"), 2.0);
						SR = (2.0 * 1e6) / 4;
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_SAMPLERATE), str);
						ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), 0);

						ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_DECIMATION));
						_stprintf_s(str, 255, "None");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
						_stprintf_s(str, 255, "2");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
						_stprintf_s(str, 255, "4");
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_DECIMATION), str);
						DecimationIdx = 2;
						ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_DECIMATION), DecimationIdx);
						DecimateEnable = 0;

						//Disable Sample Rate Display & Decimation Dialog Boxes
						Edit_Enable(GetDlgItem(hwndDlg, IDC_SAMPLERATE), 0);
						Edit_Enable(GetDlgItem(hwndDlg, IDC_DECIMATION), 0);
					}
					WinradCallBack(-1, WINRAD_SRCHANGE, 0, NULL);
					return true;
				}
				break;

			case IDC_LNAGR_SWITCH:
				if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
				{
					if (Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED)
					{
						LNAEnable = true;
                        if (AGCEnabled == mir_sdr_AGC_DISABLE)
                        {
                            mir_sdr_GetGrByFreq_fn(Frequency, (mir_sdr_BandT *)&FreqBand, (int *)&GainReduction, LNAEnable, &SystemGainReduction, mir_sdr_USE_SET_GR_ALT_MODE);
                        }
						Edit_SetText(GetDlgItem(hwndDlg, IDC_LNAGR), "LNA GR 0 dB");
						Edit_SetText(GetDlgItem(hwndDlg, IDC_LNASTATE), "LNA ON");
						//SendMessage(GetDlgItem(hwndDlg, IDC_GR_S), UDM_SETRANGE, (WPARAM)true, (LPARAM)MAKELONG(19, 78));
						SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETRANGEMIN, (WPARAM)true, (LPARAM)19);
						SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETRANGEMAX, (WPARAM)true, (LPARAM)78);
					}
					else
					{
						LNAEnable = false;
                        if (AGCEnabled == mir_sdr_AGC_DISABLE)
                        {
                            mir_sdr_GetGrByFreq_fn(Frequency, (mir_sdr_BandT *)&FreqBand, (int *)&GainReduction, LNAEnable, &SystemGainReduction, mir_sdr_USE_SET_GR_ALT_MODE);
                        }
						sprintf_s(str, sizeof(str), "%s %d %s", "LNA GR", band_LNAgain[FreqBand], "dB");
						Edit_SetText(GetDlgItem(hwndDlg, IDC_LNAGR), str);
						Edit_SetText(GetDlgItem(hwndDlg, IDC_LNASTATE), "LNA OFF");
						//SendMessage(GetDlgItem(hwndDlg, IDC_GR_S), UDM_SETRANGE, (WPARAM)true, (LPARAM)MAKELONG(band_MinGR[FreqBand], 78));
						SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETRANGEMIN, (WPARAM)true, (LPARAM)band_MinGR[FreqBand]);
						SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETRANGEMAX, (WPARAM)true, (LPARAM)78);
					}
               
					if (AGCEnabled == mir_sdr_AGC_DISABLE) 
					{
						SendMessage(GetDlgItem(hwndDlg, IDC_GAINSLIDER), TBM_SETPOS, (WPARAM)true, (LPARAM)GainReduction);
						sprintf_s(str, sizeof(str), "IF Gain Reduction %d dB", GainReduction);
						Edit_SetText(GetDlgItem(hwndDlg, IDC_IFGR), str);
						sprintf_s(GainReductionTxt, 254, "%d", GainReduction);
						Edit_SetText(GetDlgItem(hwndDlg, IDC_GR), GainReductionTxt);
						sprintf_s(str, sizeof(str), "Total System Gain Reduction %d dB", SystemGainReduction);
						Edit_SetText(GetDlgItem(hwndDlg, IDC_TOTALGR), str);
					}
					else
					{
						mir_sdr_AgcControl_fn(AGCEnabled, AGCsetpoint, 0, 0, 0, 0, LNAEnable);
					}
					return true;
				}
				break;
			}
		}
		return false;
	}

static INT_PTR CALLBACK AdvancedDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HWND hGain;
	static HBRUSH BRUSH_RED = CreateSolidBrush(RGB(255, 0, 0));
	static HBRUSH BRUSH_GREEN = CreateSolidBrush(RGB(0, 255, 0));
	int periodic;
	TCHAR str[255];
	static bool LOplanAutoLCL;
	static int LOplanLCL;
	static int DcCompensationModeLCL;
	static int RefreshPeriodMemoryLCL;
	static bool PostTunerDcCompensationLCL;

	switch (uMsg)
	{
	case WM_CLOSE:
		ShowWindow(h_AdvancedDialog, SW_HIDE);
		return true;
		break;

	case WM_HSCROLL:
		if ((HWND)lParam == GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD))
		{
			TrackingPeriod = SendMessage(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), TBM_GETPOS, (WPARAM)0, (LPARAM)0);
			TrackTime = TrackingPeriod *DCTrackTimeInterval;
			sprintf_s(str, 254, "%.2f uS", TrackTime);
			Edit_SetText(GetDlgItem(hwndDlg, IDC_TRACKTIME), str);
		}
		return true;
		break;

	case WM_DESTROY:
		h_AdvancedDialog = NULL;
		return true;
		break;

	case WM_INITDIALOG:
		// LO Planning
		if (LOplanAuto)
		{
			SendMessage(GetDlgItem(hwndDlg, IDC_LOAUTO), BM_SETCHECK, BST_CHECKED, 0);
			Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING), "Full Coverage");
			Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING1), " ");
		}
		else
		{
			if (LOplan == LO120MHz)
			{
				SendMessage(GetDlgItem(hwndDlg, IDC_LO120), BM_SETCHECK, BST_CHECKED, 0);
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING), "Coverage gap between 370MHz and 420MHz");
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING1), " ");
			}
			if (LOplan == LO144MHz)
			{
				SendMessage(GetDlgItem(hwndDlg, IDC_LO144), BM_SETCHECK, BST_CHECKED, 0);
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING), "Coverage gaps between 250MHz to 255MHz");
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING1), "Coverage gaps between 400MHz to 420MHz");
			}
			if (LOplan == LO168MHz)
			{
				SendMessage(GetDlgItem(hwndDlg, IDC_LO168), BM_SETCHECK, BST_CHECKED, 0);
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING), "Coverage gap between 250MHz and 265MHz");
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING1), " ");
			}
		}

		//Tracking period
		SendMessage(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), TBM_SETRANGEMIN, (WPARAM)true, (LPARAM)1);
		SendMessage(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), TBM_SETRANGEMAX, (WPARAM)true, (LPARAM)63);
		SendMessage(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), TBM_SETPOS, (WPARAM)true, (LPARAM)TrackingPeriod);
		TrackTime = TrackingPeriod *DCTrackTimeInterval;
		sprintf_s(str, 254, "%.2f uS", TrackTime);
		Edit_SetText(GetDlgItem(hwndDlg, IDC_TRACKTIME), str);

		//Populate the refresh period
		ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_REFRESH));
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_REFRESH), "6mS");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_REFRESH), "12mS");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_REFRESH), "24mS");
		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_REFRESH), RefreshPeriodMemory);

		//DC Offset Compensation
		if (DcCompensationMode == STATIC)
		{
			SendMessage(GetDlgItem(hwndDlg, IDC_DCSTAT), BM_SETCHECK, BST_CHECKED, 0);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_REFRESH), 0);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), 0);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKTIME), 0);
		}
		if (DcCompensationMode == ONESHOT)
		{
			SendMessage(GetDlgItem(hwndDlg, IDC_ONESHOT), BM_SETCHECK, BST_CHECKED, 0);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_REFRESH), 0);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), 1);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKTIME), 1);
		}
		if (DcCompensationMode == CONTINUOUS)
		{
			SendMessage(GetDlgItem(hwndDlg, IDC_CONTINUOUS), BM_SETCHECK, BST_CHECKED, 0);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_REFRESH), 0);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), 0);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKTIME), 0);
		}
		if (DcCompensationMode == PERIODIC1 || DcCompensationMode == PERIODIC2 || DcCompensationMode == PERIODIC3)
		{
			SendMessage(GetDlgItem(hwndDlg, IDC_PERIODIC), BM_SETCHECK, BST_CHECKED, 0);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_REFRESH), 1);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), 1);
			Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKTIME), 1);
			if (DcCompensationMode == PERIODIC1)
			{
				ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_REFRESH), 0);
			}
			if (DcCompensationMode == PERIODIC2)
			{
				ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_REFRESH), 1);
			}
			if (DcCompensationMode == PERIODIC3)
			{
				ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_REFRESH), 2);
			}
		}

		//Post Tuner DC Compensdation
		if (PostTunerDcCompensation)
			SendMessage(GetDlgItem(hwndDlg, IDC_POSTDCOFFSET), BM_SETCHECK, BST_CHECKED, 0);
		else
			SendMessage(GetDlgItem(hwndDlg, IDC_POSTDCOFFSET), BM_SETCHECK, BST_UNCHECKED, 0);

		LOplanAutoLCL = LOplanAuto;
		LOplanLCL = LOplan;
		DcCompensationModeLCL = DcCompensationMode;
		RefreshPeriodMemoryLCL = RefreshPeriodMemory;
		PostTunerDcCompensationLCL = PostTunerDcCompensation;
		return true;
		break;
			
	case WM_COMMAND:
		switch (GET_WM_COMMAND_ID(wParam, lParam))
		{
		case IDC_ADVANCED_EXIT:
			ShowWindow(h_AdvancedDialog, SW_HIDE);
			return true;
			break;

		case IDC_ADVANCED_APPLY:
         if ((PostTunerDcCompensation != PostTunerDcCompensationLCL) || (LOplanAuto != LOplanAutoLCL) || (LOplan != LOplanLCL))
         {
            PostTunerDcCompensation = PostTunerDcCompensationLCL;
            IQCompensation(PostTunerDcCompensation);
            LOplanAuto = LOplanAutoLCL;
            LOplan = LOplanLCL;
            LOMode = GetLoMode();
            DcCompensationMode = DcCompensationModeLCL;
            RefreshPeriodMemory = RefreshPeriodMemoryLCL;
            if (Running)
            {
               ReinitAll();
            }
         }
         else
         {
            if ((DcCompensationMode != DcCompensationModeLCL) || (RefreshPeriodMemory != RefreshPeriodMemoryLCL))
            {
               DcCompensationMode = DcCompensationModeLCL;
               RefreshPeriodMemory = RefreshPeriodMemoryLCL;
               mir_sdr_SetDcMode_fn(DcCompensationMode, 0);
            }
         }
			return true;
			break;

		case IDC_ADVANCED_STATIONCONFIG:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				if (h_StationConfigDialog == NULL)
					h_StationConfigDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_STATIONCONFIG_PANEL), NULL, (DLGPROC)StationConfigDlgProc);
				ShowWindow(h_StationConfigDialog, SW_SHOW);
				SetForegroundWindow(h_StationConfigDialog);
				return true;
			}

		case IDC_LOAUTO:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				LOplanAutoLCL = true;
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING), "Full Coverage");
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING1), " ");
			}
			return true;
			break;

		case IDC_LO120:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				LOplanAutoLCL = false;
				LOplanLCL = LO120MHz;
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING), "Coverage gap between 370MHz and 420MHz");
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING1), " ");
				MessageBox(NULL, "By selecting this LO PLan there will be a coverage gap between 370MHz and 420MHz", TEXT("SDRplay ExtIO DLL Warning"), MB_OK | MB_SYSTEMMODAL | MB_TOPMOST | MB_ICONEXCLAMATION);
			}
			return true;
			break;

		case IDC_LO144:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				LOplanAutoLCL = false;
				LOplanLCL = LO144MHz;
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING), "Coverage gaps between 250MHz to 255MHz");
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING1), "Coverage gaps between 400MHz to 420MHz");
				MessageBox(NULL, "By selecting this LO PLan there will be coverage gaps between 250MHz to 255MHz and 400MHz to 420MHz", TEXT("SDRplay ExtIO DLL Warning"), MB_OK | MB_SYSTEMMODAL | MB_TOPMOST | MB_ICONEXCLAMATION);
			}
			return true;
			break;

		case IDC_LO168:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				LOplanAutoLCL = false;
				LOplanLCL = LO168MHz;
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING), "Coverage gap between 250MHz and 265MHz");
				Edit_SetText(GetDlgItem(hwndDlg, IDC_LOWARNING1), " ");
				MessageBox(NULL, "By selecting this LO PLan there will be a coverage gap between 250MHz and 265MHz", TEXT("SDRplay ExtIO DLL Warning"), MB_OK | MB_SYSTEMMODAL | MB_TOPMOST | MB_ICONEXCLAMATION);
			}
			return true;
			break;

		case IDC_POSTDCOFFSET:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				if (Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED)
					PostTunerDcCompensationLCL = true;
				else
					PostTunerDcCompensationLCL = false;
			}
			return true;
			break;

		case IDC_DCSTAT:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				DcCompensationModeLCL = STATIC;
				Edit_Enable(GetDlgItem(hwndDlg, IDC_REFRESH), 0);
				Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), 0);
				Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKTIME), 0);
			}
			return true;
			break;

		case IDC_ONESHOT:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				DcCompensationModeLCL = ONESHOT;
				Edit_Enable(GetDlgItem(hwndDlg, IDC_REFRESH), 0);
				Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), 1);
				Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKTIME), 1);
			}
			return true;
			break;

		case IDC_CONTINUOUS:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				DcCompensationModeLCL = CONTINUOUS;
				Edit_Enable(GetDlgItem(hwndDlg, IDC_REFRESH), 0);
				Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), 0);
				Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKTIME), 0);
			}
			return true;
			break;

		case IDC_PERIODIC:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				Edit_Enable(GetDlgItem(hwndDlg, IDC_REFRESH), 1);
				Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKINGPERIOD), 1);
				Edit_Enable(GetDlgItem(hwndDlg, IDC_TRACKTIME), 1);
				periodic = ComboBox_GetCurSel(GetDlgItem(hwndDlg, IDC_REFRESH));
				if (periodic == 0)
				{
					DcCompensationModeLCL = PERIODIC1;
					RefreshPeriodMemoryLCL = 0;
				}
				if (periodic == 1)
				{
					DcCompensationModeLCL = PERIODIC2;
					RefreshPeriodMemoryLCL = 1;
				}
				if (periodic == 2)
				{
					DcCompensationModeLCL = PERIODIC3;
					RefreshPeriodMemoryLCL = 2;
				}
			}
			return true;
			break;

		case IDC_REFRESH:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
			{
				periodic = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));
				if (periodic == 0)
				{
					DcCompensationModeLCL = PERIODIC1;
					RefreshPeriodMemoryLCL = 0;
				}
				if (periodic == 1)
				{
					DcCompensationModeLCL = PERIODIC2;
					RefreshPeriodMemoryLCL = 1;
				}
				if (periodic == 2)
				{
					DcCompensationModeLCL = PERIODIC3;
					RefreshPeriodMemoryLCL = 2;
				}
			}
			return true;
			break;
		}
	}
	return false;
}

static INT_PTR CALLBACK ProfilesDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
	static HBRUSH hBrushGreen = CreateSolidBrush(RGB(0, 255, 0));
	static HBRUSH hBrushBlack = CreateSolidBrush(RGB(0, 0, 0));
	OPENFILENAME ofn = { 0 };
	TCHAR szFileName[MAX_PATH] = _T("");
	DWORD nBytesWritten = 0;
	HANDLE hFile = { 0 };
	TCHAR str[255];
	TCHAR subprofile[50];
	TCHAR vers[5] = TEXT("0001");
	TCHAR readvers[5];
	int _BandwidthIdx = 0;
	int _GainReduction = 0;
	bool locAGC;

//	HBITMAP hbitmap1, hbitmap2;

	switch (uMsg)
	{
	case WM_INITDIALOG:
		LoadProfilesReg(hwndDlg);
		//hbitmap1 = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_PROF_BUTTON_DISABLED));
		//hbitmap2 = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_PROF_BUTTON_ASSIGN));
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_ONE), BM_SETIMAGE, 0, (LPARAM)hbitmap1);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_TWO), BM_SETIMAGE, 0, (LPARAM)hbitmap1);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_THREE), BM_SETIMAGE, 0, (LPARAM)hbitmap1);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_FOUR), BM_SETIMAGE, 0, (LPARAM)hbitmap1);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_FIVE), BM_SETIMAGE, 0, (LPARAM)hbitmap1);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_SIX), BM_SETIMAGE, 0, (LPARAM)hbitmap1);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_SEVEN), BM_SETIMAGE, 0, (LPARAM)hbitmap1);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_EIGHT), BM_SETIMAGE, 0, (LPARAM)hbitmap1);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_ASSIGN_ONE), BM_SETIMAGE, 0, (LPARAM)hbitmap2);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_ASSIGN_TWO), BM_SETIMAGE, 0, (LPARAM)hbitmap2);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_ASSIGN_THREE), BM_SETIMAGE, 0, (LPARAM)hbitmap2);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_ASSIGN_FOUR), BM_SETIMAGE, 0, (LPARAM)hbitmap2);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_ASSIGN_FIVE), BM_SETIMAGE, 0, (LPARAM)hbitmap2);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_ASSIGN_SIX), BM_SETIMAGE, 0, (LPARAM)hbitmap2);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_ASSIGN_SEVEN), BM_SETIMAGE, 0, (LPARAM)hbitmap2);
		//SendMessage(GetDlgItem(hwndDlg, IDC_PROF_BUTTON_ASSIGN_EIGHT), BM_SETIMAGE, 0, (LPARAM)hbitmap2);
		return true;
		break;

	case WM_CLOSE:
		ShowWindow(h_ProfilesDialog, SW_HIDE);
		return true;
		break;

	case WM_DESTROY:
		h_ProfilesDialog = NULL;
		return true;
		break;

	case WM_COMMAND:
		switch (GET_WM_COMMAND_ID(wParam, lParam))
		{
		case IDC_PROF_EXIT:
			ShowWindow(h_ProfilesDialog, SW_HIDE);
			return true;
			break;

		case IDC_PROF_BUTTON_ONE:
			// load profile 1
			if (_tcscmp(p1fname, "") != 0)
			{
				LoadProfile(1);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "[P1]");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
			}
			return true;
			break;

		case IDC_PROF_BUTTON_TWO:
			// load profile 2
			if (_tcscmp(p2fname, "") != 0)
			{
				LoadProfile(2);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "[P2]");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
			}
			return true;
			break;

		case IDC_PROF_BUTTON_THREE:
			// load profile 3
			if (_tcscmp(p3fname, "") != 0)
			{
				LoadProfile(3);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "[P3]");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
			}
			return true;
			break;

		case IDC_PROF_BUTTON_FOUR:
			// load profile 4
			if (_tcscmp(p4fname, "") != 0)
			{
				LoadProfile(4);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "[P4]");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
			}
			return true;
			break;

		case IDC_PROF_BUTTON_FIVE:
			// load profile 5
			if (_tcscmp(p5fname, "") != 0)
			{
				LoadProfile(5);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "[P5]");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
			}
			return true;
			break;

		case IDC_PROF_BUTTON_SIX:
			// load profile 6
			if (_tcscmp(p6fname, "") != 0)
			{
				LoadProfile(6);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "[P6]");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
			}
			return true;
			break;

		case IDC_PROF_BUTTON_SEVEN:
			// load profile 7
			if (_tcscmp(p7fname, "") != 0)
			{
				LoadProfile(7);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "[P7]");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "P8");
			}
			return true;
			break;

		case IDC_PROF_BUTTON_EIGHT:
			// load profile 8
			if (_tcscmp(p8fname, "") != 0)
			{
				LoadProfile(8);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_ONE), "P1");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_TWO), "P2");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_THREE), "P3");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FOUR), "P4");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_FIVE), "P5");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SIX), "P6");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_SEVEN), "P7");
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_BUTTON_EIGHT), "[P8]");
			}
			return true;
			break;

		case IDC_LOADPROFILE_P1:
			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = h_ProfilesDialog;
			ofn.lpstrFilter = _T("RSP Profiles (*.rsp)\0*.rsp\0\0");
			ofn.lpstrFile = szFileName;
			ofn.lpstrDefExt = _T("rsp");
			ofn.nMaxFile = _MAX_PATH;
			ofn.lpstrTitle = _T("Load RSP Profile");
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

			if (GetOpenFileName(&ofn))
			{
				_tcscpy_s(p1fname, szFileName);
				PathRemoveExtension(szFileName);
				_tcsnccpy_s(subprofile, PathFindFileName(szFileName), 30);
				_stprintf_s(str, TEXT("\nPROFILE STORED: %s"), subprofile);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_DESC_ONE), str);
			}
			break;

		case IDC_LOADPROFILE_P2:
			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = h_ProfilesDialog;
			ofn.lpstrFilter = _T("RSP Profiles (*.rsp)\0*.rsp\0\0");
			ofn.lpstrFile = szFileName;
			ofn.lpstrDefExt = _T("rsp");
			ofn.nMaxFile = _MAX_PATH;
			ofn.lpstrTitle = _T("Load RSP Profile");
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

			if (GetOpenFileName(&ofn))
			{
				_tcscpy_s(p2fname, szFileName);
				PathRemoveExtension(szFileName);
				_tcsnccpy_s(subprofile, PathFindFileName(szFileName), 30);
				_stprintf_s(str, TEXT("\nPROFILE STORED: %s"), subprofile);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_DESC_TWO), str);
			}
			break;

		case IDC_LOADPROFILE_P3:
			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = h_ProfilesDialog;
			ofn.lpstrFilter = _T("RSP Profiles (*.rsp)\0*.rsp\0\0");
			ofn.lpstrFile = szFileName;
			ofn.lpstrDefExt = _T("rsp");
			ofn.nMaxFile = _MAX_PATH;
			ofn.lpstrTitle = _T("Load RSP Profile");
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

			if (GetOpenFileName(&ofn))
			{
				_tcscpy_s(p3fname, szFileName);
				PathRemoveExtension(szFileName);
				_tcsnccpy_s(subprofile, PathFindFileName(szFileName), 30);
				_stprintf_s(str, TEXT("\nPROFILE STORED: %s"), subprofile);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_DESC_THREE), str);
			}
			break;

		case IDC_LOADPROFILE_P4:
			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = h_ProfilesDialog;
			ofn.lpstrFilter = _T("RSP Profiles (*.rsp)\0*.rsp\0\0");
			ofn.lpstrFile = szFileName;
			ofn.lpstrDefExt = _T("rsp");
			ofn.nMaxFile = _MAX_PATH;
			ofn.lpstrTitle = _T("Load RSP Profile");
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

			if (GetOpenFileName(&ofn))
			{
				_tcscpy_s(p4fname, szFileName);
				PathRemoveExtension(szFileName);
				_tcsnccpy_s(subprofile, PathFindFileName(szFileName), 30);
				_stprintf_s(str, TEXT("\nPROFILE STORED: %s"), subprofile);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_DESC_FOUR), str);
			}
			break;

		case IDC_LOADPROFILE_P5:
			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = h_ProfilesDialog;
			ofn.lpstrFilter = _T("RSP Profiles (*.rsp)\0*.rsp\0\0");
			ofn.lpstrFile = szFileName;
			ofn.lpstrDefExt = _T("rsp");
			ofn.nMaxFile = _MAX_PATH;
			ofn.lpstrTitle = _T("Load RSP Profile");
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

			if (GetOpenFileName(&ofn))
			{
				_tcscpy_s(p5fname, szFileName);
				PathRemoveExtension(szFileName);
				_tcsnccpy_s(subprofile, PathFindFileName(szFileName), 30);
				_stprintf_s(str, TEXT("\nPROFILE STORED: %s"), subprofile);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_DESC_FIVE), str);
			}
			break;

		case IDC_LOADPROFILE_P6:
			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = h_ProfilesDialog;
			ofn.lpstrFilter = _T("RSP Profiles (*.rsp)\0*.rsp\0\0");
			ofn.lpstrFile = szFileName;
			ofn.lpstrDefExt = _T("rsp");
			ofn.nMaxFile = _MAX_PATH;
			ofn.lpstrTitle = _T("Load RSP Profile");
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

			if (GetOpenFileName(&ofn))
			{
				_tcscpy_s(p6fname, szFileName);
				PathRemoveExtension(szFileName);
				_tcsnccpy_s(subprofile, PathFindFileName(szFileName), 30);
				_stprintf_s(str, TEXT("\nPROFILE STORED: %s"), subprofile);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_DESC_SIX), str);
			}
			break;

		case IDC_LOADPROFILE_P7:
			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = h_ProfilesDialog;
			ofn.lpstrFilter = _T("RSP Profiles (*.rsp)\0*.rsp\0\0");
			ofn.lpstrFile = szFileName;
			ofn.lpstrDefExt = _T("rsp");
			ofn.nMaxFile = _MAX_PATH;
			ofn.lpstrTitle = _T("Load RSP Profile");
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

			if (GetOpenFileName(&ofn))
			{
				_tcscpy_s(p7fname, szFileName);
				PathRemoveExtension(szFileName);
				_tcsnccpy_s(subprofile, PathFindFileName(szFileName), 30);
				_stprintf_s(str, TEXT("\nPROFILE STORED: %s"), subprofile);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_DESC_SEVEN), str);
			}
			break;

		case IDC_LOADPROFILE_P8:
			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = h_ProfilesDialog;
			ofn.lpstrFilter = _T("RSP Profiles (*.rsp)\0*.rsp\0\0");
			ofn.lpstrFile = szFileName;
			ofn.lpstrDefExt = _T("rsp");
			ofn.nMaxFile = _MAX_PATH;
			ofn.lpstrTitle = _T("Load RSP Profile");
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

			if (GetOpenFileName(&ofn))
			{
				_tcscpy_s(p8fname, szFileName);
				PathRemoveExtension(szFileName);
				_tcsnccpy_s(subprofile, PathFindFileName(szFileName), 30);
				_stprintf_s(str, TEXT("\nPROFILE STORED: %s"), subprofile);
				Edit_SetText(GetDlgItem(h_ProfilesDialog, IDC_PROF_DESC_EIGHT), str);
			}
			break;

		case IDC_LoadProfile:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				ofn.lStructSize = sizeof(OPENFILENAME);
				ofn.hwndOwner = h_ProfilesDialog;
				ofn.lpstrFilter = _T("RSP Profiles (*.rsp)\0*.rsp\0\0");
				ofn.lpstrFile = szFileName;
				ofn.lpstrDefExt = _T("rsp");
				ofn.nMaxFile = _MAX_PATH;
				ofn.lpstrTitle = _T("Load RSP Profile");
				ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

				if (GetOpenFileName(&ofn))
				{
					hFile = CreateFile(szFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

					// Read file format version number
					ReadFile(hFile, &readvers, sizeof(readvers), &nBytesWritten, NULL);

					if (strcmp(readvers, vers) != 0) {
						CloseHandle(hFile);
						_stprintf_s(str, TEXT("File format not supported: %s"), readvers);
						MessageBox(NULL, str, "RSP Profile format error", MB_OK | MB_TOPMOST | MB_ICONERROR);
					}
					else
					{
						// IF Mode
						ReadFile(hFile, &IFMode, sizeof(IFMode), &nBytesWritten, NULL);
						ReadFile(hFile, &IFmodeIdx, sizeof(IFmodeIdx), &nBytesWritten, NULL);

						// IF Bandwidth
						ReadFile(hFile, &Bandwidth, sizeof(Bandwidth), &nBytesWritten, NULL);
						ReadFile(hFile, &_BandwidthIdx, sizeof(_BandwidthIdx), &nBytesWritten, NULL);

						BandwidthIdx = _BandwidthIdx;
						ComboBox_SetCurSel(GetDlgItem(h_dialog, IDC_IFMODE), IFmodeIdx);
						ComboBox_ResetContent(GetDlgItem(h_dialog, IDC_IFBW));
						_stprintf_s(str, 255, "200 kHz");
						ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
						_stprintf_s(str, 255, "300 kHz");
						ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
						_stprintf_s(str, 255, "600 kHz");
						ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
						_stprintf_s(str, 255, "1.536 MHz");
						ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
						if (IFmodeIdx == 0)
						{
							_stprintf_s(str, 255, "5.000 MHz");
							ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
							_stprintf_s(str, 255, "6.000 MHz");
							ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
							_stprintf_s(str, 255, "7.000 MHz");
							ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
							_stprintf_s(str, 255, "8.000 MHz");
							ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
						}
						ComboBox_SetCurSel(GetDlgItem(h_dialog, IDC_IFBW), BandwidthIdx);

						// Sample Rate
						ReadFile(hFile, &SampleRateIdx, sizeof(SampleRateIdx), &nBytesWritten, NULL);
						ComboBox_SetCurSel(GetDlgItem(h_dialog, IDC_SAMPLERATE), SampleRateIdx);

						// Decimation
						ReadFile(hFile, &DecimationIdx, sizeof(DecimationIdx), &nBytesWritten, NULL);
						ComboBox_SetCurSel(GetDlgItem(h_dialog, IDC_DECIMATION), DecimationIdx);

						// Gain Reduction
						ReadFile(hFile, &_GainReduction, sizeof(_GainReduction), &nBytesWritten, NULL);
						GainReduction = _GainReduction;
						sprintf_s(GainReductionTxt, 254, "%d", GainReduction);
						Edit_SetText(GetDlgItem(h_dialog, IDC_GR), GainReductionTxt);
						SendMessage(GetDlgItem(h_dialog, IDC_GAINSLIDER), TBM_SETPOS, (WPARAM)true, (LPARAM)GainReduction);

						// AGC Setpoint
						ReadFile(hFile, &AGCsetpoint, sizeof(AGCsetpoint), &nBytesWritten, NULL);
						sprintf_s(AGCsetpointTxt, 254, "%d", AGCsetpoint);
						Edit_SetText(GetDlgItem(h_dialog, IDC_SETPOINT), AGCsetpointTxt);

						// Frequency Offset
						ReadFile(hFile, &FqOffsetPPM, sizeof(FqOffsetPPM), &nBytesWritten, NULL);
						sprintf_s(FreqoffsetTxt, 254, "%.2f", FqOffsetPPM);
						Edit_SetText(GetDlgItem(h_dialog, IDC_PPM), FreqoffsetTxt);

						// AGC Enable
						ReadFile(hFile, &locAGC, sizeof(locAGC), &nBytesWritten, NULL);
						AGCEnabled = mir_sdr_AGC_5HZ;
						if (!locAGC)
							AGCEnabled = mir_sdr_AGC_DISABLE;

						if (AGCEnabled != mir_sdr_AGC_DISABLE)
						{
							SendMessage(GetDlgItem(h_dialog, IDC_RSPAGC), BM_SETCHECK, (WPARAM)(1), 0);
							Edit_Enable(GetDlgItem(h_dialog, IDC_GR), 0);
							Edit_Enable(GetDlgItem(h_dialog, IDC_GAINSLIDER), 0);
						}
						else
						{
							SendMessage(GetDlgItem(h_dialog, IDC_RSPAGC), BM_SETCHECK, (WPARAM)(0), 0);
							Edit_Enable(GetDlgItem(h_dialog, IDC_GR), 1);
							Edit_Enable(GetDlgItem(h_dialog, IDC_GAINSLIDER), 1);
						}

						// LO Plan
						ReadFile(hFile, &LOplan, sizeof(LOplan), &nBytesWritten, NULL);
						ReadFile(hFile, &LOplanAuto, sizeof(LOplanAuto), &nBytesWritten, NULL);
						if (LOplanAuto)
						{
							SendMessage(GetDlgItem(h_dialog, IDC_LOAUTO), BM_SETCHECK, BST_CHECKED, 0);
							Edit_SetText(GetDlgItem(h_dialog, IDC_LOWARNING), "Full Coverage");
							Edit_SetText(GetDlgItem(h_dialog, IDC_LOWARNING1), " ");
						}
						else
						{
							if (LOplan == LO120MHz)
							{
								SendMessage(GetDlgItem(h_dialog, IDC_LO120), BM_SETCHECK, BST_CHECKED, 0);
								Edit_SetText(GetDlgItem(h_dialog, IDC_LOWARNING), "Coverage gap between 370MHz and 420MHz");
								Edit_SetText(GetDlgItem(h_dialog, IDC_LOWARNING1), " ");
							}
							if (LOplan == LO144MHz)
							{
								SendMessage(GetDlgItem(h_dialog, IDC_LO144), BM_SETCHECK, BST_CHECKED, 0);
								Edit_SetText(GetDlgItem(h_dialog, IDC_LOWARNING), "Coverage gaps between 250MHz to 255MHz");
								Edit_SetText(GetDlgItem(h_dialog, IDC_LOWARNING1), "Coverage gaps between 400MHz to 420MHz");
							}
							if (LOplan == LO168MHz)
							{
								SendMessage(GetDlgItem(h_dialog, IDC_LO168), BM_SETCHECK, BST_CHECKED, 0);
								Edit_SetText(GetDlgItem(h_dialog, IDC_LOWARNING), "Coverage gap between 250MHz and 265MHz");
								Edit_SetText(GetDlgItem(h_dialog, IDC_LOWARNING1), " ");
							}
						}

						WinradCallBack(-1, WINRAD_SRCHANGE, 0, NULL);

                  if (Running)
                  {
                     ReinitAll();
                  }

						// DC compensation mode
						ReadFile(hFile, &DcCompensationMode, sizeof(DcCompensationMode), &nBytesWritten, NULL);

						// Tracking period
						ReadFile(hFile, &TrackingPeriod, sizeof(TrackingPeriod), &nBytesWritten, NULL);

						// Refresh Period
						ReadFile(hFile, &RefreshPeriodMemory, sizeof(RefreshPeriodMemory), &nBytesWritten, NULL);

						// Post Tuner DC compensation
						ReadFile(hFile, &PostTunerDcCompensation, sizeof(PostTunerDcCompensation), &nBytesWritten, NULL);

						CloseHandle(hFile);

						PathRemoveExtension(szFileName);
						_tcsnccpy_s(subprofile, PathFindFileName(szFileName), 30);
						_stprintf_s(str, TEXT("Current Profile: %s"), subprofile);
						Edit_SetText(GetDlgItem(h_dialog, IDC_ProfileFileName), str);
					}
				}
			}
			return true;

		case IDC_SaveProfile:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
			{
				TCHAR writevers[5] = TEXT("0002");
				int _BandwidthIdx = BandwidthIdx;
				int _GainReduction = GainReduction;
				ofn.lStructSize = sizeof(OPENFILENAME);
				ofn.hwndOwner = h_ProfilesDialog;
				ofn.lpstrFilter = _T("RSP Profiles (*.rsp)\0*.rsp\0\0");
				ofn.lpstrFile = szFileName;
				ofn.lpstrDefExt = _T("rsp");
				ofn.nMaxFile = _MAX_PATH;
				ofn.lpstrTitle = _T("Save RSP Profile");
				ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_EXPLORER;

				if (GetSaveFileName(&ofn))
				{
					hFile = CreateFile(szFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

					WriteFile(hFile, &writevers, sizeof(writevers), &nBytesWritten, NULL);								// Version
					WriteFile(hFile, &IFMode, sizeof(IFMode), &nBytesWritten, NULL);									// IF Mode
					WriteFile(hFile, &IFmodeIdx, sizeof(IFmodeIdx), &nBytesWritten, NULL);
					WriteFile(hFile, &Bandwidth, sizeof(Bandwidth), &nBytesWritten, NULL);								// IF Bandwidth
					WriteFile(hFile, &_BandwidthIdx, sizeof(_BandwidthIdx), &nBytesWritten, NULL);
					WriteFile(hFile, &SampleRateIdx, sizeof(SampleRateIdx), &nBytesWritten, NULL);						// Sample Rate
					WriteFile(hFile, &DecimationIdx, sizeof(DecimationIdx), &nBytesWritten, NULL);						// Decimation
					WriteFile(hFile, &_GainReduction, sizeof(_GainReduction), &nBytesWritten, NULL);					// Gain Reduction
					WriteFile(hFile, &AGCsetpoint, sizeof(AGCsetpoint), &nBytesWritten, NULL);							// AGC Setpoint
					WriteFile(hFile, &FqOffsetPPM, sizeof(FqOffsetPPM), &nBytesWritten, NULL);							// Frequency Offset
					locAGC = true;
					if (AGCEnabled == mir_sdr_AGC_DISABLE)
						locAGC = false;
					WriteFile(hFile, &locAGC, sizeof(locAGC), &nBytesWritten, NULL);									// AGC Enable
					WriteFile(hFile, &LOplan, sizeof(LOplan), &nBytesWritten, NULL);									// LO plan
					WriteFile(hFile, &LOplanAuto, sizeof(LOplanAuto), &nBytesWritten, NULL);							// LO plan Auto
					WriteFile(hFile, &DcCompensationMode, sizeof(DcCompensationMode), &nBytesWritten, NULL);			// DC compensation mode
					WriteFile(hFile, &TrackingPeriod, sizeof(TrackingPeriod), &nBytesWritten, NULL);					// Tracking period
					WriteFile(hFile, &RefreshPeriodMemory, sizeof(RefreshPeriodMemory), &nBytesWritten, NULL);			// Refresh Period
					WriteFile(hFile, &PostTunerDcCompensation, sizeof(PostTunerDcCompensation), &nBytesWritten, NULL);	// Post Tuner DC compensation
					WriteFile(hFile, &LNAEnable, sizeof(LNAEnable), &nBytesWritten, NULL);								// LNA Enable

					CloseHandle(hFile);

					PathRemoveExtension(szFileName);
					_tcsnccpy_s(subprofile, PathFindFileName(szFileName), 30);
					_stprintf_s(str, TEXT("Current Profile: %s"), subprofile);
					Edit_SetText(GetDlgItem(h_dialog, IDC_ProfileFileName), str);
				}
			}
			return true;
		}
	}
	return false;
}

void LoadProfile(int profile)
{
	OPENFILENAME ofn = { 0 };
	DWORD nBytesWritten = 0;
	HANDLE hFile = { 0 };
	TCHAR str[255];
	TCHAR subprofile[50];
	TCHAR copiedfn[MAX_PATH];
	TCHAR filename[MAX_PATH];
	bool locAGC;

	if (profile == 1)
	{
		_tcscpy_s(filename, p1fname);
	}
	else if (profile == 2)
	{
		_tcscpy_s(filename, p2fname);
	}
	else if (profile == 3)
	{
		_tcscpy_s(filename, p3fname);
	}
	else if (profile == 4)
	{
		_tcscpy_s(filename, p4fname);
	}
	else if (profile == 5)
	{
		_tcscpy_s(filename, p5fname);
	}
	else if (profile == 6)
	{
		_tcscpy_s(filename, p6fname);
	}
	else if (profile == 7)
	{
		_tcscpy_s(filename, p7fname);
	}
	else
	{
		_tcscpy_s(filename, p8fname);
	}

	TCHAR vers[5] = TEXT("0002");
	TCHAR readvers[5];
	int _BandwidthIdx = 0;
	int _GainReduction = 0;
	int LNAEN = 0;
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = h_ProfilesDialog;
	ofn.lpstrFilter = _T("RSP Profiles (*.rsp)\0*.rsp\0\0");
	ofn.lpstrFile = filename;
	ofn.lpstrDefExt = _T("rsp");
	ofn.nMaxFile = _MAX_PATH;
	ofn.lpstrTitle = _T("Load RSP Profile");
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

	hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	// Read file format version number
	ReadFile(hFile, &readvers, sizeof(readvers), &nBytesWritten, NULL);

	if (strcmp(readvers, vers) != 0)
	{
		CloseHandle(hFile);
		_stprintf_s(str, TEXT("File not found or format not supported: %s"), readvers);
		MessageBox(NULL, str, "RSP Profile file error", MB_OK | MB_TOPMOST | MB_ICONERROR);
	}
	else
	{
		// IF Mode
		ReadFile(hFile, &IFMode, sizeof(IFMode), &nBytesWritten, NULL);
		ReadFile(hFile, &IFmodeIdx, sizeof(IFmodeIdx), &nBytesWritten, NULL);

		// IF Bandwidth
		ReadFile(hFile, &Bandwidth, sizeof(Bandwidth), &nBytesWritten, NULL);
		ReadFile(hFile, &_BandwidthIdx, sizeof(_BandwidthIdx), &nBytesWritten, NULL);

		BandwidthIdx = _BandwidthIdx;
		ComboBox_SetCurSel(GetDlgItem(h_dialog, IDC_IFMODE), IFmodeIdx);
		ComboBox_ResetContent(GetDlgItem(h_dialog, IDC_IFBW));
		_stprintf_s(str, 255, "200 kHz");
		ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
		_stprintf_s(str, 255, "300 kHz");
		ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
		_stprintf_s(str, 255, "600 kHz");
		ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
		_stprintf_s(str, 255, "1.536 MHz");
		ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
		if (IFmodeIdx == 0)
		{
			_stprintf_s(str, 255, "5.000 MHz");
			ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
			_stprintf_s(str, 255, "6.000 MHz");
			ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
			_stprintf_s(str, 255, "7.000 MHz");
			ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
			_stprintf_s(str, 255, "8.000 MHz");
			ComboBox_AddString(GetDlgItem(h_dialog, IDC_IFBW), str);
		}
		ComboBox_SetCurSel(GetDlgItem(h_dialog, IDC_IFBW), BandwidthIdx);

		// Sample Rate
		ReadFile(hFile, &SampleRateIdx, sizeof(SampleRateIdx), &nBytesWritten, NULL);
		ComboBox_SetCurSel(GetDlgItem(h_dialog, IDC_SAMPLERATE), SampleRateIdx);

		// Gain Reduction
		ReadFile(hFile, &_GainReduction, sizeof(_GainReduction), &nBytesWritten, NULL);
		GainReduction = _GainReduction;
		sprintf_s(GainReductionTxt, 254, "%d", GainReduction);
		Edit_SetText(GetDlgItem(h_dialog, IDC_GR), GainReductionTxt);
		SendMessage(GetDlgItem(h_dialog, IDC_GAINSLIDER), TBM_SETPOS, (WPARAM)true, (LPARAM)GainReduction);

		// AGC Setpoint
		ReadFile(hFile, &AGCsetpoint, sizeof(AGCsetpoint), &nBytesWritten, NULL);
		sprintf_s(AGCsetpointTxt, 254, "%d", AGCsetpoint);
		Edit_SetText(GetDlgItem(h_dialog, IDC_SETPOINT), AGCsetpointTxt);

		// Frequency Offset
		ReadFile(hFile, &FqOffsetPPM, sizeof(FqOffsetPPM), &nBytesWritten, NULL);
		sprintf_s(FreqoffsetTxt, 254, "%.2f", FqOffsetPPM);
		Edit_SetText(GetDlgItem(h_dialog, IDC_PPM), FreqoffsetTxt);

		// AGC Enable
		ReadFile(hFile, &locAGC, sizeof(locAGC), &nBytesWritten, NULL);
		AGCEnabled = mir_sdr_AGC_5HZ;
		if (!locAGC)
			AGCEnabled = mir_sdr_AGC_DISABLE;
		if (AGCEnabled != mir_sdr_AGC_DISABLE)
		{
			SendMessage(GetDlgItem(h_dialog, IDC_RSPAGC), BM_SETCHECK, (WPARAM)(1), 0);
			Edit_Enable(GetDlgItem(h_dialog, IDC_GR), 0);
			Edit_Enable(GetDlgItem(h_dialog, IDC_GAINSLIDER), 0);
		}
		else
		{
			SendMessage(GetDlgItem(h_dialog, IDC_RSPAGC), BM_SETCHECK, (WPARAM)(0), 0);
			Edit_Enable(GetDlgItem(h_dialog, IDC_GR), 1);
			Edit_Enable(GetDlgItem(h_dialog, IDC_GAINSLIDER), 1);
		}

		// LO Plan
		ReadFile(hFile, &LOplan, sizeof(LOplan), &nBytesWritten, NULL);
		ReadFile(hFile, &LOplanAuto, sizeof(LOplanAuto), &nBytesWritten, NULL);
		if (LOplanAuto)
		{
			SendMessage(GetDlgItem(h_AdvancedDialog, IDC_LOAUTO), BM_SETCHECK, BST_CHECKED, 0);
			Edit_SetText(GetDlgItem(h_AdvancedDialog, IDC_LOWARNING), "Full Coverage");
			Edit_SetText(GetDlgItem(h_AdvancedDialog, IDC_LOWARNING1), " ");
		}
		else
		{
			if (LOplan == LO120MHz)
			{
				SendMessage(GetDlgItem(h_AdvancedDialog, IDC_LO120), BM_SETCHECK, BST_CHECKED, 0);
				Edit_SetText(GetDlgItem(h_AdvancedDialog, IDC_LOWARNING), "Coverage gap between 370MHz and 420MHz");
				Edit_SetText(GetDlgItem(h_AdvancedDialog, IDC_LOWARNING1), " ");
			}
			if (LOplan == LO144MHz)
			{
				SendMessage(GetDlgItem(h_AdvancedDialog, IDC_LO144), BM_SETCHECK, BST_CHECKED, 0);
				Edit_SetText(GetDlgItem(h_AdvancedDialog, IDC_LOWARNING), "Coverage gaps between 250MHz to 255MHz");
				Edit_SetText(GetDlgItem(h_AdvancedDialog, IDC_LOWARNING1), "Coverage gaps between 400MHz to 420MHz");
			}
			if (LOplan == LO168MHz)
			{
				SendMessage(GetDlgItem(h_AdvancedDialog, IDC_LO168), BM_SETCHECK, BST_CHECKED, 0);
				Edit_SetText(GetDlgItem(h_AdvancedDialog, IDC_LOWARNING), "Coverage gap between 250MHz and 265MHz");
				Edit_SetText(GetDlgItem(h_AdvancedDialog, IDC_LOWARNING1), " ");
			}
		}

		WinradCallBack(-1, WINRAD_SRCHANGE, 0, NULL);

      if (Running)
      {
         ReinitAll();
      }

		// DC compensation mode
		ReadFile(hFile, &DcCompensationMode, sizeof(DcCompensationMode), &nBytesWritten, NULL);

		// Tracking period
		ReadFile(hFile, &TrackingPeriod, sizeof(TrackingPeriod), &nBytesWritten, NULL);

		// Refresh Period
		ReadFile(hFile, &RefreshPeriodMemory, sizeof(RefreshPeriodMemory), &nBytesWritten, NULL);

		// Post Tuner DC compensation
		ReadFile(hFile, &PostTunerDcCompensation, sizeof(PostTunerDcCompensation), &nBytesWritten, NULL);

		// LNA Enable
		ReadFile(hFile, &LNAEN, sizeof(LNAEN), &nBytesWritten, NULL);
		if (LNAEN == 1)
		{
         LNAEnable = true;
         mir_sdr_GetGrByFreq_fn(Frequency, (mir_sdr_BandT *)NULL, (int *)&GainReduction, LNAEnable, &SystemGainReduction, mir_sdr_USE_SET_GR_ALT_MODE);
         //SendMessage(GetDlgItem(h_dialog, IDC_LNAGR_SWITCH), BM_SETCHECK, BST_CHECKED, 0);
			Edit_SetText(GetDlgItem(h_dialog, IDC_LNAGR), "LNA GR 0 dB");
			Edit_SetText(GetDlgItem(h_dialog, IDC_LNASTATE), "LNA ON");
			sprintf_s(str, sizeof(str), "Total System Gain Reduction %d dB", SystemGainReduction);
			Edit_SetText(GetDlgItem(h_dialog, IDC_TOTALGR), str);
		}
		else
		{
         LNAEnable = false;
         mir_sdr_GetGrByFreq_fn(Frequency, (mir_sdr_BandT *)NULL, (int *)&GainReduction, LNAEnable, &SystemGainReduction, mir_sdr_USE_SET_GR_ALT_MODE);
         //SendMessage(GetDlgItem(h_dialog, IDC_LNAGR_SWITCH), BM_SETCHECK, BST_UNCHECKED, 0);
			Edit_SetText(GetDlgItem(h_dialog, IDC_LNAGR), "LNA GR 24 dB");
			Edit_SetText(GetDlgItem(h_dialog, IDC_LNASTATE), "LNA OFF");
			sprintf_s(str, sizeof(str), "Total System Gain Reduction %d dB", SystemGainReduction);
			Edit_SetText(GetDlgItem(h_dialog, IDC_TOTALGR), str);
		}

		CloseHandle(hFile);

		_tcscpy_s(copiedfn, filename);
		PathRemoveExtension(copiedfn);
		_tcsnccpy_s(subprofile, PathFindFileName(copiedfn), 30);
		_stprintf_s(str, TEXT("Current Profile: (P%d) %s"), profile, subprofile);
		Edit_SetText(GetDlgItem(h_dialog, IDC_ProfileFileName), str);
	}
}

void LoadProfilesReg(HWND localDlg)
{
	HKEY Settingskey;
	int error;
	TCHAR tmpfn[MAX_PATH];
	TCHAR subprofile[40];
	TCHAR str[255];
	DWORD strSize = MAX_PATH;
	DWORD type = REG_SZ;

	if (!loadedProfiles)
	{

		error = RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\SDRplay\\Settings"), 0, KEY_ALL_ACCESS, &Settingskey);
		if (error == ERROR_SUCCESS)
		{
			error = RegQueryValueEx(Settingskey, "P1", NULL, &type, (LPBYTE)&p1fname, &strSize);
			if (error == ERROR_SUCCESS)
			{
				ifstream ifile(p1fname);
				if (ifile)
				{
					_tcscpy_s(tmpfn, p1fname);
					PathRemoveExtension(tmpfn);
					_tcsnccpy_s(subprofile, PathFindFileName(tmpfn), 30);
					_stprintf_s(str, TEXT("\nPROFILE ASSIGNED: %s"), subprofile);
					Edit_SetText(GetDlgItem(localDlg, IDC_PROF_DESC_ONE), str);
				}
			}

			error = RegQueryValueEx(Settingskey, "P2", NULL, &type, (LPBYTE)&p2fname, &strSize);
			if (error == ERROR_SUCCESS)
			{
				ifstream ifile(p2fname);
				if (ifile)
				{
					_tcscpy_s(tmpfn, p2fname);
					PathRemoveExtension(tmpfn);
					_tcsnccpy_s(subprofile, PathFindFileName(tmpfn), 30);
					_stprintf_s(str, TEXT("\nPROFILE ASSIGNED: %s"), subprofile);
					Edit_SetText(GetDlgItem(localDlg, IDC_PROF_DESC_TWO), str);
				}
			}

			error = RegQueryValueEx(Settingskey, "P3", NULL, &type, (LPBYTE)&p3fname, &strSize);
			if (error == ERROR_SUCCESS)
			{
				ifstream ifile(p3fname);
				if (ifile)
				{
					_tcscpy_s(tmpfn, p3fname);
					PathRemoveExtension(tmpfn);
					_tcsnccpy_s(subprofile, PathFindFileName(tmpfn), 30);
					_stprintf_s(str, TEXT("\nPROFILE ASSIGNED: %s"), subprofile);
					Edit_SetText(GetDlgItem(localDlg, IDC_PROF_DESC_THREE), str);
				}
			}

			error = RegQueryValueEx(Settingskey, "P4", NULL, &type, (LPBYTE)&p4fname, &strSize);
			if (error == ERROR_SUCCESS)
			{
				ifstream ifile(p4fname);
				if (ifile)
				{
					_tcscpy_s(tmpfn, p4fname);
					PathRemoveExtension(tmpfn);
					_tcsnccpy_s(subprofile, PathFindFileName(tmpfn), 30);
					_stprintf_s(str, TEXT("\nPROFILE ASSIGNED: %s"), subprofile);
					Edit_SetText(GetDlgItem(localDlg, IDC_PROF_DESC_FOUR), str);
				}
			}

			error = RegQueryValueEx(Settingskey, "P5", NULL, &type, (LPBYTE)&p5fname, &strSize);
			if (error == ERROR_SUCCESS)
			{
				ifstream ifile(p5fname);
				if (ifile)
				{
					_tcscpy_s(tmpfn, p5fname);
					PathRemoveExtension(tmpfn);
					_tcsnccpy_s(subprofile, PathFindFileName(tmpfn), 30);
					_stprintf_s(str, TEXT("\nPROFILE ASSIGNED: %s"), subprofile);
					Edit_SetText(GetDlgItem(localDlg, IDC_PROF_DESC_FIVE), str);
				}
			}

			error = RegQueryValueEx(Settingskey, "P6", NULL, &type, (LPBYTE)&p6fname, &strSize);
			if (error == ERROR_SUCCESS)
			{
				ifstream ifile(p6fname);
				if (ifile)
				{
					_tcscpy_s(tmpfn, p1fname);
					PathRemoveExtension(tmpfn);
					_tcsnccpy_s(subprofile, PathFindFileName(tmpfn), 30);
					_stprintf_s(str, TEXT("\nPROFILE ASSIGNED: %s"), subprofile);
					Edit_SetText(GetDlgItem(localDlg, IDC_PROF_DESC_SIX), str);
				}
			}

			error = RegQueryValueEx(Settingskey, "P7", NULL, &type, (LPBYTE)&p7fname, &strSize);
			if (error == ERROR_SUCCESS)
			{
				ifstream ifile(p7fname);
				if (ifile)
				{
					_tcscpy_s(tmpfn, p7fname);
					PathRemoveExtension(tmpfn);
					_tcsnccpy_s(subprofile, PathFindFileName(tmpfn), 30);
					_stprintf_s(str, TEXT("\nPROFILE ASSIGNED: %s"), subprofile);
					Edit_SetText(GetDlgItem(localDlg, IDC_PROF_DESC_SEVEN), str);
				}
			}

			error = RegQueryValueEx(Settingskey, "P8", NULL, &type, (LPBYTE)&p8fname, &strSize);
			if (error == ERROR_SUCCESS)
			{
				ifstream ifile(p8fname);
				if (ifile)
				{
					_tcscpy_s(tmpfn, p8fname);
					PathRemoveExtension(tmpfn);
					_tcsnccpy_s(subprofile, PathFindFileName(tmpfn), 30);
					_stprintf_s(str, TEXT("\nPROFILE ASSIGNED: %s"), subprofile);
					Edit_SetText(GetDlgItem(localDlg, IDC_PROF_DESC_EIGHT), str);
				}
			}
		}
		RegCloseKey(Settingskey);
	}
	loadedProfiles = true;
}

static INT_PTR CALLBACK HelpDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
	TCHAR str[255];
	TCHAR tregPath[255];
	size_t out;

	switch (uMsg)
	{
	case WM_INITDIALOG:
		_stprintf_s(str, TEXT("API Version: %s"), apiVersion);
		Edit_SetText(GetDlgItem(hwndDlg, IDC_APIVER), str);
		_stprintf_s(str, TEXT("API Path:\n%s"), apiPath);
		Edit_SetText(GetDlgItem(hwndDlg, IDC_APIPATH), str);
		wcstombs_s(&out, tregPath, sizeof(tregPath), regPath, 255);
		_stprintf_s(str, TEXT("Registry Path:\n%s"), tregPath);
		Edit_SetText(GetDlgItem(hwndDlg, IDC_REGPATH), str);
		return true;
		break;

	case WM_CLOSE:
		ShowWindow(h_HelpDialog, SW_HIDE);
		return true;
		break;

	case WM_DESTROY:
		h_HelpDialog = NULL;
		return true;
		break;

	case WM_COMMAND:
		switch (GET_WM_COMMAND_ID(wParam, lParam))
		{
		case IDC_HELP_PANEL_EXIT:
			ShowWindow(h_HelpDialog, SW_HIDE);
			return true;
			break;

		case IDC_HELP_LINK:
			ShellExecute(0, 0, "http://www.sdrplay.com/docs/sdrplay_extio_user_guide.pdf", 0, 0, SW_SHOW);
			return true;
			break;
		}
	}
	return false;
}

static INT_PTR CALLBACK StationDlgProc(HWND, UINT uMsg, WPARAM, LPARAM)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		return true;
		break;

	case WM_CLOSE:
		ShowWindow(h_StationDialog, SW_HIDE);
		return true;
		break;

	case WM_DESTROY:
		h_StationDialog = NULL;
		return true;
		break;

	}
	return false;
}

void UpdateProfile(void)
{
	if (profileChanged)
	{
		Edit_SetText(GetDlgItem(h_dialog, IDC_PROFILE_CHANGED), "(M)");
	}
	else
	{
		Edit_SetText(GetDlgItem(h_dialog, IDC_PROFILE_CHANGED), "");
	}
	return;
}

BOOL EqualsMajorVersion(DWORD majorVersion)
{
	OSVERSIONINFOEX osVersionInfo;
	::ZeroMemory(&osVersionInfo, sizeof(OSVERSIONINFOEX));
	osVersionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	osVersionInfo.dwMajorVersion = majorVersion;
	ULONGLONG maskCondition = ::VerSetConditionMask(0, VER_MAJORVERSION, VER_EQUAL);
	return ::VerifyVersionInfo(&osVersionInfo, VER_MAJORVERSION, maskCondition);
}
BOOL EqualsMinorVersion(DWORD minorVersion)
{
	OSVERSIONINFOEX osVersionInfo;
	::ZeroMemory(&osVersionInfo, sizeof(OSVERSIONINFOEX));
	osVersionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	osVersionInfo.dwMinorVersion = minorVersion;
	ULONGLONG maskCondition = ::VerSetConditionMask(0, VER_MINORVERSION, VER_EQUAL);
	return ::VerifyVersionInfo(&osVersionInfo, VER_MINORVERSION, maskCondition);
}

static INT_PTR CALLBACK StationConfigDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	int i;
#ifdef DEBUG_STATION
	int arch = 0;
	SYSTEM_INFO Info;
	ZeroMemory(&Info, sizeof(SYSTEM_INFO));
	PGNSI pGetNativeSystemInfo = (PGNSI)GetProcAddress(GetModuleHandle(_T("kernel32.dll")), "GetNativeSystemInfo");
	if (pGetNativeSystemInfo != NULL)
		pGetNativeSystemInfo(&Info);
	else
		GetSystemInfo(&Info);

	if (Info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
		arch = 64;
	else
		arch = 32;
#endif

	switch (uMsg)
	{
	case WM_INITDIALOG:
		if (stationLookup)
		{
			SendMessage(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_ENABLE), BM_SETCHECK, BST_CHECKED, 0);
			if (!localDBExists)
				Edit_SetText(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_STATUS), "Database Status: No route to host");
			else
				Edit_SetText(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_STATUS), "Database Status: Good");
		}
		else
		{
			SendMessage(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_ENABLE), BM_SETCHECK, BST_UNCHECKED, 0);
			Edit_SetText(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_STATUS), "Database Status: disabled");
		}

		if (workOffline)
		{
			SendMessage(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_OFFLINE), BM_SETCHECK, BST_CHECKED, 0);
			if (localDBExists)
				Edit_SetText(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_STATUS), "Database Status: Good (offline)");
			else
				Edit_SetText(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_STATUS), "Database Status: No database and offline");
		}
		else
			SendMessage(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_OFFLINE), BM_SETCHECK, BST_UNCHECKED, 0);

#ifdef DEBUG_STATION
		char tmpString[1024];
		if (EqualsMajorVersion(7) && EqualsMinorVersion(1))
			sprintf_s(tmpString, 1024, "major: 7, minor: 1, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else if (EqualsMajorVersion(7) && EqualsMinorVersion(0))
			sprintf_s(tmpString, 1024, "major: 7, minor: 0, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else if (EqualsMajorVersion(6) && EqualsMinorVersion(7))
			sprintf_s(tmpString, 1024, "major: 6, minor: 7, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else if (EqualsMajorVersion(6) && EqualsMinorVersion(6))
			sprintf_s(tmpString, 1024, "major: 6, minor: 6, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else if (EqualsMajorVersion(6) && EqualsMinorVersion(5))
			sprintf_s(tmpString, 1024, "major: 6, minor: 5, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else if (EqualsMajorVersion(6) && EqualsMinorVersion(4))
			sprintf_s(tmpString, 1024, "major: 6, minor: 4, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else if (EqualsMajorVersion(6) && EqualsMinorVersion(3))
			sprintf_s(tmpString, 1024, "major: 6, minor: 3, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else if (EqualsMajorVersion(6) && EqualsMinorVersion(2))
			sprintf_s(tmpString, 1024, "major: 6, minor: 2, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else if (EqualsMajorVersion(6) && EqualsMinorVersion(1))
			sprintf_s(tmpString, 1024, "major: 6, minor: 1, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else if (EqualsMajorVersion(6) && EqualsMinorVersion(0))
			sprintf_s(tmpString, 1024, "major: 6, minor: 0, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else if (EqualsMajorVersion(5) && EqualsMinorVersion(2))
			sprintf_s(tmpString, 1024, "major: 5, minor: 2, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else if (EqualsMajorVersion(5) && EqualsMinorVersion(1))
			sprintf_s(tmpString, 1024, "major: 5, minor: 1, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		else
			sprintf_s(tmpString, 1024, "major: 0, minor: 0, dll: %d, arch: %d", ((sizeof(void *)) * 8), arch);
		OutputDebugString(tmpString);
#endif

		ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_TAC));
		for (i = 0; i < (sizeof(targetAreaCodes) / sizeof(targetAreaCodes[0])); i++)
		{
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_TAC), targetAreaCodes[i].description);
		}
		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_TAC), TACIndex);

		ComboBox_ResetContent(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_ITU));
		for (i = 0; i < (sizeof(ituCodes) / sizeof(ituCodes[0])); i++)
		{
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_ITU), ituCodes[i].countryName);
		}
		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_STATIONCONFIG_ITU), ITUIndex);

		return true;
		break;

	case WM_CLOSE:
		ShowWindow(h_StationConfigDialog, SW_HIDE);
		return true;
		break;

	case WM_DESTROY:
		h_StationConfigDialog = NULL;
		return true;
		break;

	case WM_COMMAND:
		switch (GET_WM_COMMAND_ID(wParam, lParam))
		{
		case IDC_STATIONCONFIG_PANEL_EXIT:
			ShowWindow(h_StationConfigDialog, SW_HIDE);
			return true;
			break;

		case IDC_STATIONCONFIG_ENABLE:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BST_UNCHECKED)
			{
				if (Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED) //it is checked
				{
					stationLookup = true;
				}
				else
				{
					stationLookup = false;
				}
			}
			return true;
			break;

		case IDC_STATIONCONFIG_OFFLINE:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == BST_UNCHECKED)
			{
				if (Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED) //it is checked
				{
					workOffline = true;
				}
				else
				{
					workOffline = false;
				}
			}
			return true;
			break;
		}
	}
	return false;
}
