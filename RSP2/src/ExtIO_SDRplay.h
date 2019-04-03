// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the LIBSDRplay_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// LIBSDRplay_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.

#ifdef LIBSDRplay_EXPORTS
#define LIBSDRplay_API __declspec(dllexport)
#else
#define LIBSDRplay_API __declspec(dllimport)
#endif

extern HMODULE hInst;
void LoadProfile(int);
void LoadProfilesReg(HWND);
void LoadFreqsReg(HWND);
int RemoveFreqReg(TCHAR[3], struct freqStore);
void UpdateProfile(void);