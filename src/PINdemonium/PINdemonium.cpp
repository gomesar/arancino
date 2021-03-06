#pragma once

#include <stdio.h>
#include "pin.H"
#include "OepFinder.h"
#include "Report.h"
#include <time.h>
#include  "Debug.h"
#include "Config.h"
#include "FilterHandler.h"
#include "HookFunctions.h"
#include "HookSyscalls.h"
#include "PolymorphicCodeHandlerModule.h"
#include "PINShield.h"
#include "md5.h"
namespace W {
	#include <windows.h>
}


OepFinder oepf;
PINshield thider;
HookFunctions hookFun;
clock_t tStart;
ProcInfo *proc_info = ProcInfo::getInstance();
PolymorphicCodeHandlerModule pcpatcher;

//------------------------------Custom option for our FindOEPpin.dll-------------------------------------------------------------------------

KNOB <UINT32> KnobInterWriteSetAnalysis(KNOB_MODE_WRITEONCE, "pintool",
    "iwae", "0" , "specify if you want or not to track the inter_write_set analysis dumps and how many jump");

KNOB <BOOL> KnobAntiEvasion(KNOB_MODE_WRITEONCE, "pintool",
    "antiev", "false" , "specify if you want or not to activate the anti evasion engine");

KNOB <BOOL> KnobAntiEvasionINSpatcher(KNOB_MODE_WRITEONCE, "pintool",
    "antiev-ins", "false" , "specify if you want or not to activate the single patching of evasive instruction as int2e, fsave...");

KNOB <BOOL> KnobAntiEvasionSuspiciousRead(KNOB_MODE_WRITEONCE, "pintool",
    "antiev-sread", "false" , "specify if you want or not to activate the handling of suspicious reads");

KNOB <BOOL> KnobAntiEvasionSuspiciousWrite(KNOB_MODE_WRITEONCE, "pintool",
    "antiev-swrite", "false" , "specify if you want or not to activate the handling of suspicious writes");

KNOB <BOOL> KnobUnpacking(KNOB_MODE_WRITEONCE, "pintool",
    "unp", "false" , "specify if you want or not to activate the unpacking engine");

KNOB <UINT32> KnobSkipDump(KNOB_MODE_WRITEONCE, "pintool",
    "skip", "0" , "specify how many times you want to skip the dump process whe wxorx rule is broken");

KNOB <BOOL> KnobAdvancedIATFixing(KNOB_MODE_WRITEONCE, "pintool",
    "adv-iatfix", "false" , "specify if you want or not to activate the advanced IAT fix technique");

KNOB <BOOL> KnobPolymorphicCodePatch(KNOB_MODE_WRITEONCE, "pintool",
    "poly-patch", "false" , "specify if you want or not to activate the patch in order to avoid crash during the instrumentation of polymorphic code");

KNOB <BOOL> KnobNullyfyUnknownIATEntry(KNOB_MODE_WRITEONCE, "pintool",
    "nullify-unk-iat", "false" , "specify if you want or not to nullify the IAT entry not detected as correct API by the tool\n NB: THIS OPTION WORKS ONLY IF THE OPTION adv-iatfix IS ACTIVE!");

KNOB <string> KnobPluginSelector(KNOB_MODE_WRITEONCE, "pintool",
    "plugin", "" , "specify the name of the plugin you want to launch if the IAT reconstructor fails (EX : PINdemoniumStolenAPIPlugin.dll)");

//------------------------------Custom option for our FindOEPpin.dll-------------------------------------------------------------------------


// This function is called when the application exits
// - print out the information relative to the current run
VOID Fini(INT32 code, VOID *v){
	//inspect the write set at the end of the execution
	WxorXHandler *wxorxHandler = WxorXHandler::getInstance();
	//MYINFO("WRITE SET SIZE: %d", wxorxHandler->getWritesSet().size());
	//get the execution time
	MYPRINT("\n\n\nTotal execution Time: %.2fs", (double)(clock() - tStart)/CLOCKS_PER_SEC);
	CLOSELOG();
	Report::getInstance()->closeReport();
}

BOOL followChild(CHILD_PROCESS childProcess, VOID *val)
{

	printf("[INFO] A new process has been spawned!\n");
	MYINFO("---------------------------------------------------");
	MYINFO("-----------A NEW PROCESS HAS BEEN SPAWNED----------");
	MYINFO("-------------[PinDemonium injected]---------------");
	MYINFO("---------------------------------------------------");
	return 1;
}

// - usage 
INT32 Usage(){
	PIN_ERROR("This Pintool unpacks common packers\n" + KNOB_BASE::StringKnobSummary() + "\n");
	return -1;
}

// - Get initial entropy
// - Get PE section data 
// - Add filtered library
// - Add protected libraries 
void imageLoadCallback(IMG img,void *){
	Section item;
	static int va_hooked = 0;
	ProcInfo *proc_info = ProcInfo::getInstance();
	FilterHandler *filterHandler = FilterHandler::getInstance();
	//get the initial entropy of the PE
	//we have to consder only the main executable and av�void the libraries
	if(IMG_IsMainExecutable(img)){		
		ADDRINT startAddr = IMG_LowAddress(img);
		ADDRINT endAddr = IMG_HighAddress(img);
		proc_info->setMainIMGAddress(startAddr, endAddr);
		//get the  address of the first instruction
		proc_info->setFirstINSaddress(IMG_Entry(img));
		//get the program name
		proc_info->setProcName(IMG_Name(img));
		//get the initial entropy
		MYINFO("----------------------------------------------");
		float initial_entropy = proc_info->GetEntropy();
		proc_info->setInitialEntropy(initial_entropy);
		MYINFO("----------------------------------------------");	
		//create Report File
		Report::getInstance()->initializeReport(proc_info->getProcName(), startAddr, endAddr , initial_entropy);
		//retrieve the section of the PE
		for( SEC sec= IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec) ){
			item.name = SEC_Name(sec);
			item.begin = SEC_Address(sec);
			item.end = item.begin + SEC_Size(sec);
			proc_info->insertSection(item);
		}
		proc_info->PrintSections();
	}
	//build the filtered libtrary list
	ADDRINT startAddr = IMG_LowAddress(img);
	ADDRINT endAddr = IMG_HighAddress(img);
	const string name = IMG_Name(img); 
	if(!IMG_IsMainExecutable(img)){
		
		//*** If you need to protect other sections of other dll put them here ***
		if(name.find("ntdll")!= std::string::npos){		
		  for( SEC sec= IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec) ){
			if(strcmp(SEC_Name(sec).c_str(),".text")==0){
				MYINFO("Adding NTDLL %08x  %08x",SEC_Address(sec),SEC_Address(sec)+SEC_Size(sec));
				proc_info->addProtectedSection(SEC_Address(sec),SEC_Address(sec)+SEC_Size(sec));
			}
	      }
		}
		// check if there are some fuction that has top be hooked in this DLL
		hookFun.hookDispatcher(img);
		// check if we have to filter this library during thwe instrumentation
		proc_info->addLibrary(name,startAddr,endAddr);
		if(filterHandler->IsNameInFilteredArray(name)){
			filterHandler->addToFilteredLibrary(name,startAddr,endAddr);
			MYINFO("Added to the filtered array the module %s\n" , name);
		}
	}
}

// trigger the instrumentation routine for each instruction
void Instruction(INS ins,void *v){
	// check the current mode of operation
	Config *config = Config::getInstance();
	if(config->ANTIEVASION_MODE){
		thider.avoidEvasion(ins);
	}
	
	if(config->UNPACKING_MODE){
		oepf.IsCurrentInOEP(ins);
	}	
}

// trigger the instrumentation routine for each trace collected (useful in order to spiot polymorphic code on the current trace)
VOID Trace(TRACE trace,void *v){
	// polymorphic code handler
	pcpatcher.inspectTrace(trace);
}


// - retrive the stack base address
static VOID OnThreadStart(THREADID, CONTEXT *ctxt, INT32, VOID *){
	ADDRINT stackBase = PIN_GetContextReg(ctxt, REG_STACK_PTR);
	ProcInfo *pInfo = ProcInfo::getInstance();
	pInfo->addThreadStackAddress(stackBase);
	pInfo->addThreadTebAddress();
	//MYINFO("-----------------a NEW Thread started!--------------------\n");
}

// - if the flag is pecified start pin as launched with the flag appdebug
void initDebug(){
	DEBUG_MODE mode;
	mode._type = DEBUG_CONNECTION_TYPE_TCP_SERVER;
	mode._options = DEBUG_MODE_OPTION_STOP_AT_ENTRY;
	PIN_SetDebugMode(&mode);
}

// - set the option for the current run
void ConfigureTool(){	

	

	
	Config *config = Config::getInstance();
	config->ANTIEVASION_MODE = KnobAntiEvasion.Value();
	config->ANTIEVASION_MODE_INS_PATCHING = KnobAntiEvasionINSpatcher.Value();
	config->ANTIEVASION_MODE_SREAD = KnobAntiEvasionSuspiciousRead.Value();
	config->ANTIEVASION_MODE_SWRITE = KnobAntiEvasionSuspiciousWrite.Value();
	
	config->UNPACKING_MODE = KnobUnpacking.Value();
	config->INTER_WRITESET_ANALYSIS_ENABLE = KnobInterWriteSetAnalysis.Value();	
	config->ADVANCED_IAT_FIX = KnobAdvancedIATFixing.Value();
	config->POLYMORPHIC_CODE_PATCH = KnobPolymorphicCodePatch.Value();
	config->NULLIFY_UNK_IAT_ENTRY = KnobNullyfyUnknownIATEntry.Value();
	config->SKIP_DUMP = KnobSkipDump.Value();
	if(KnobInterWriteSetAnalysis.Value() >= 1 && KnobInterWriteSetAnalysis.Value() <= Config::MAX_JUMP_INTER_WRITE_SET_ANALYSIS ){
		config->WRITEINTERVAL_MAX_NUMBER_JMP = KnobInterWriteSetAnalysis.Value();
	}
	else{
		//MYWARN("Invalid number of jumps to track, se to default value: 2\n");
		config->WRITEINTERVAL_MAX_NUMBER_JMP = 2; // default value is 2 if we have invalid value 
	}
	//get the selected plugin or return an erro if it doen't exist
	if(KnobPluginSelector.Value().compare("") != 0){
		config->CALL_PLUGIN_FLAG = true;
		config->PLUGIN_FULL_PATH = config->getScyllaPluginsPath() + KnobPluginSelector.Value();
		W::DWORD fileAttrib = W::GetFileAttributes(config->PLUGIN_FULL_PATH.c_str());
		//file doesn't exist
		if(fileAttrib == 0xFFFFFFFF){
			printf("[ERROR] THE SELECTED PLUGIN DOES NOT EXIST!\n\n");
			exit(-1);
		}
	}
	//don't call any plugin if it isn't selected
	else{
		config->CALL_PLUGIN_FLAG = false;
	}
	//set filtered write
	FilterHandler::getInstance()->setFilters(config->getFilteredWrites());
}

// - if an exception is found returns all the information about it (DEBUG purposes)
EXCEPT_HANDLING_RESULT ExceptionHandler(THREADID tid, EXCEPTION_INFO *pExceptInfo, PHYSICAL_CONTEXT *pPhysCtxt, VOID *v){	
	MYINFO("******Caught Exception:******\n");
	MYINFO("%s",PIN_ExceptionToString(pExceptInfo).c_str());
	MYINFO("*****Continue to search a valid exception handler******\n");
	return EHR_CONTINUE_SEARCH;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[]){

	//If we want to debug the program manually setup the proper options in order to attach an external debugger
	if(Config::ATTACH_DEBUGGER){
		initDebug();
	}
	
	//get the start time of the execution (benchmark)
	tStart = clock();	
	// Initialize pin
	PIN_InitSymbols();
	if (PIN_Init(argc, argv)) return Usage();
	//Register PIN Callbacks
	INS_AddInstrumentFunction(Instruction,0);
	PIN_AddThreadStartFunction(OnThreadStart, 0);
	IMG_AddInstrumentFunction(imageLoadCallback, 0);
	PIN_AddFiniFunction(Fini, 0);
	PIN_AddInternalExceptionHandler(ExceptionHandler,NULL);
	PIN_AddFollowChildProcessFunction(followChild, NULL);
	
	printf("[INFO] Configuring Pintool\n");
	//get theknob args
	ConfigureTool();	
	if(Config::getInstance()->POLYMORPHIC_CODE_PATCH){
		TRACE_AddInstrumentFunction(Trace,0);
	}
	proc_info->addProcAddresses();

	//init the hooking system
	HookSyscalls::enumSyscalls();
	HookSyscalls::initHooks();
	printf("[INFO] Starting instrumented program\n\n");
	//MYINFO(" knob inizio %d %d %d",Config::getInstance()->getDumpNumber(), Config::getInstance()->getDumpNumber(),Config::getInstance()->WRITEINTERVAL_MAX_NUMBER_JMP);
	PIN_StartProgram();	
	return 0;
	
}
