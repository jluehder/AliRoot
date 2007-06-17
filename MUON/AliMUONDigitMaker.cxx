/**************************************************************************
 * Copyright(c) 1998-1999, ALICE Experiment at CERN, All rights reserved. *
 *                                                                        *
 * Author: The ALICE Off-line Project.                                    *
 * Contributors are mentioned in the code where appropriate.              *
 *                                                                        *
 * Permission to use, copy, modify and distribute this software and its   *
 * documentation strictly for non-commercial purposes is hereby granted   *
 * without fee, provided that the above copyright notice appears in all   *
 * copies and that both the copyright notice and this permission notice   *
 * appear in the supporting documentation. The authors make no claims     *
 * about the suitability of this software for any purpose. It is          *
 * provided "as is" without express or implied warranty.                  *
 **************************************************************************/

/// \class AliMUONDigitMaker
/// MUON Digit maker from rawdata.
///
/// Raw2Digits:
/// Using real mapping  for tracker
/// Indranil Das (Adapted for runloader: Ch. Finck) july 05
///
/// Implemented non-constant buspatch numbers for tracking
/// with correct DDL id.
/// (Ch. Finck, dec 05)
///
/// Add reader for scaler trigger events
/// Use memcpy instead of assignment elt by elt
/// (Ch. Finck, Jan 06)
///
/// Using new interface with AliMUONRawStreamTracker(Trigger)
/// (New interface of AliMUONRawReader class)
/// (further details could be found in Alice-note)
/// (Ch. Finck, March 06)
///
/// Add (S)Digit maker tracker (for free)
/// and for trigger. Create trigger inverse mapping.
///
/// \author Ch. Finck, oct 06 

#include "AliMUONDigitMaker.h"

#include "AliLog.h"
#include "AliMUONDDLTrigger.h"
#include "AliMUONDarcHeader.h"
#include "AliMUONVDigit.h"
#include "AliMUONVDigitStore.h"
#include "AliMUONGlobalTrigger.h"
#include "AliMUONLocalStruct.h"
#include "AliMUONLocalTrigger.h"
#include "AliMUONLocalTriggerBoard.h"
#include "AliMUONRawStreamTracker.h"
#include "AliMUONRawStreamTrigger.h"
#include "AliMUONRegHeader.h"
#include "AliMUONTriggerCircuit.h"
#include "AliMUONTriggerCrate.h"
#include "AliMUONTriggerCrateStore.h"
#include "AliMUONVTriggerStore.h"
#include "AliMpCathodType.h"
#include "AliMpDDLStore.h"
#include "AliMpDEManager.h"
#include "AliMpPad.h"
#include "AliMpSegmentation.h"
#include "AliMpVSegmentation.h"
#include "AliRawReader.h"
#include <TArrayS.h>

/// \cond CLASSIMP
ClassImp(AliMUONDigitMaker) // Class implementation in ROOT context
/// \endcond

//__________________________________________________________________________
AliMUONDigitMaker::AliMUONDigitMaker()
  : TObject(),
    fScalerEvent(kFALSE),
    fMakeTriggerDigits(kFALSE),
    fRawStreamTracker(new AliMUONRawStreamTracker()),    
    fRawStreamTrigger(new AliMUONRawStreamTrigger()),    
    fCrateManager(0x0),
    fTrackerTimer(),
    fTriggerTimer(),
    fMappingTimer(),
    fDigitStore(0x0),
    fTriggerStore(0x0)
{
  /// ctor 

  AliDebug(1,"");

  // Standard Constructor

  fTrackerTimer.Start(kTRUE); fTrackerTimer.Stop();
  fTriggerTimer.Start(kTRUE); fTriggerTimer.Stop();
  fMappingTimer.Start(kTRUE); fMappingTimer.Stop();
  
  SetMakeTriggerDigits();

}

//__________________________________________________________________________
AliMUONDigitMaker::~AliMUONDigitMaker()
{
  /// clean up
  /// and time processing measure

  delete fRawStreamTracker;
  delete fRawStreamTrigger;

  AliDebug(1, Form("Execution time for MUON tracker : R:%.2fs C:%.2fs",
               fTrackerTimer.RealTime(),fTrackerTimer.CpuTime()));
  AliDebug(1, Form("   Execution time for MUON tracker (mapping calls part) "
               ": R:%.2fs C:%.2fs",
               fMappingTimer.RealTime(),fMappingTimer.CpuTime()));
  AliDebug(1, Form("Execution time for MUON trigger : R:%.2fs C:%.2fs",
               fTriggerTimer.RealTime(),fTriggerTimer.CpuTime()));

}

//____________________________________________________________________
Int_t AliMUONDigitMaker::Raw2Digits(AliRawReader* rawReader, 
                                    AliMUONVDigitStore* digitStore,
                                    AliMUONVTriggerStore* triggerStore)
{
  /// Main method to creates digit
  /// for tracker 
  /// and trigger

  AliDebug(1,Form("rawReader=%p digitStore=%p triggerStore=%p",
                  rawReader,digitStore,triggerStore));
  
  fDigitStore = digitStore;
  fTriggerStore = triggerStore;
  
  if (!fDigitStore && !fTriggerStore)
  {
    AliError("No digit or trigger store given. Nothing to do...");
    return kFALSE;
  }
  
  if ( fDigitStore ) 
  {
    fDigitStore->Clear(); // insure we start with an empty container
    ReadTrackerDDL(rawReader);
  }
  
  if ( fTriggerStore || fMakeTriggerDigits ) 
  {
    if ( fTriggerStore ) fTriggerStore->Clear();
    if ( fMakeTriggerDigits && !fDigitStore ) 
    {
      AliError("Asking for trigger digits but digitStore is null");
    }
    else
    {
      ReadTriggerDDL(rawReader);
    }
  }
  
  return kTRUE;
}

//____________________________________________________________________
Int_t AliMUONDigitMaker::ReadTrackerDDL(AliRawReader* rawReader)
{
  /// Reading tracker DDL
  /// filling the fDigitStore container, which must not be null

  AliDebug(1,"");
  
  fTrackerTimer.Start(kFALSE);

  // elex info
  Int_t    buspatchId;
  UChar_t  channelId;
  UShort_t manuId;
  UShort_t charge; 

  fRawStreamTracker->SetReader(rawReader);
  fRawStreamTracker->First();
  
  while ( fRawStreamTracker->Next(buspatchId,manuId,channelId,charge) )
  {    
    // getting DE from buspatch
    Int_t detElemId = AliMpDDLStore::Instance()->GetDEfromBus(buspatchId);

    const AliMpVSegmentation* seg 
      = AliMpSegmentation::Instance()->GetMpSegmentationByElectronics(detElemId, 
                                                                      manuId);  

    AliMp::CathodType cathodeType = AliMpDEManager::GetCathod(detElemId, 
                                                              seg->PlaneType());

    AliMpPad pad = seg->PadByLocation(AliMpIntPair(manuId,channelId),kFALSE);
    
    if (!pad.IsValid())
    {
      AliError(Form("No pad for detElemId: %d, manuId: %d, channelId: %d",
                    detElemId, manuId, channelId));
      continue;
    } 
    
    AliMUONVDigit* digit = fDigitStore->Add(detElemId,manuId,channelId,cathodeType,
                                            AliMUONVDigitStore::kDeny);
    if (!digit)
    {
      AliError(Form("Digit DE %04d Manu %04d Channel %02d could not be added",
                    detElemId, manuId, channelId));
      continue;
    }
    
    digit->SetPadXY(pad.GetIndices().GetFirst(),
                   pad.GetIndices().GetSecond());
    
	  digit->SetADC(charge);

  }
  
  fTrackerTimer.Stop();

  return kTRUE;
}

//____________________________________________________________________
Int_t AliMUONDigitMaker::ReadTriggerDDL(AliRawReader* rawReader)
{
  /// reading tracker DDL
  /// filling the fTriggerStore container, which must not be null

  AliDebug(1,"");
  
  AliMUONDDLTrigger*       ddlTrigger      = 0x0;
  AliMUONDarcHeader*       darcHeader      = 0x0;
  AliMUONRegHeader*        regHeader       = 0x0;
  AliMUONLocalStruct*      localStruct     = 0x0;

  Int_t loCircuit;

  fTriggerTimer.Start(kFALSE);

  fRawStreamTrigger->SetReader(rawReader);

  while (fRawStreamTrigger->NextDDL()) 
  {
    ddlTrigger = fRawStreamTrigger->GetDDLTrigger();
    darcHeader = ddlTrigger->GetDarcHeader();
    
    // fill global trigger information
    if (fTriggerStore) 
    {
      if (darcHeader->GetGlobalFlag()) 
      {
          AliMUONGlobalTrigger globalTrigger;
          globalTrigger.SetFromGlobalResponse(darcHeader->GetGlobalOutput());
          fTriggerStore->SetGlobal(globalTrigger);
      }
    }
    
    Int_t nReg = darcHeader->GetRegHeaderEntries();
    
    for(Int_t iReg = 0; iReg < nReg ;iReg++)
    {   //reg loop
      
      // crate info
      if (!fCrateManager) AliFatal("Crate Store not defined");
      AliMUONTriggerCrate* crate = fCrateManager->Crate(fRawStreamTrigger->GetDDL(), iReg);
      
      if (!crate) 
        AliWarning(Form("Missing crate number %d in DDL %d\n", iReg, fRawStreamTrigger->GetDDL()));
      
      TObjArray *boards  = crate->Boards();
      
      regHeader =  darcHeader->GetRegHeaderEntry(iReg);
      
      Int_t nLocal = regHeader->GetLocalEntries();
      for(Int_t iLocal = 0; iLocal < nLocal; iLocal++) 
      {  
        
        localStruct = regHeader->GetLocalEntry(iLocal);
        
        // if card exist
        if (localStruct) {
          
          AliMUONLocalTriggerBoard* localBoard = 
          (AliMUONLocalTriggerBoard*)boards->At(localStruct->GetId()+1);
          
          // skip copy cards
          if( !(loCircuit = localBoard->GetNumber()) )
            continue;
          
          if (fTriggerStore) 
          {
            // fill local trigger
            AliMUONLocalTrigger localTrigger;
            localTrigger.SetLocalStruct(loCircuit, *localStruct);
            fTriggerStore->Add(localTrigger);
          } 
          
          if ( fMakeTriggerDigits )
          {
            //FIXEME should find something better than a TArray
            TArrayS xyPattern[2];
            xyPattern[0].Set(4);
            xyPattern[1].Set(4);
            
            xyPattern[0].AddAt(localStruct->GetX1(),0);
            xyPattern[0].AddAt(localStruct->GetX2(),1);
            xyPattern[0].AddAt(localStruct->GetX3(),2);
            xyPattern[0].AddAt(localStruct->GetX4(),3);
            
            xyPattern[1].AddAt(localStruct->GetY1(),0);
            xyPattern[1].AddAt(localStruct->GetY2(),1);
            xyPattern[1].AddAt(localStruct->GetY3(),2);
            xyPattern[1].AddAt(localStruct->GetY4(),3);
            
            TriggerDigits(loCircuit, xyPattern, *fDigitStore);
          }          
        } // if triggerY
      } // iLocal
    } // iReg
  } // NextDDL
  
  fTriggerTimer.Stop();

  return kTRUE;

}

//____________________________________________________________________
Int_t AliMUONDigitMaker::TriggerDigits(Int_t nBoard, 
                                       TArrayS* xyPattern,
                                       AliMUONVDigitStore& digitStore) const
{
  /// make digits for trigger from pattern, and add them to digitStore

  // loop over x1-4 and y1-4
  for (Int_t iChamber = 0; iChamber < 4; ++iChamber)
  {
    for (Int_t iCath = 0; iCath < 2; ++iCath)
    {
      Int_t pattern = (Int_t)xyPattern[iCath].At(iChamber); 
      if (!pattern) continue;
      
      // get detElemId
      AliMUONTriggerCircuit triggerCircuit;
      AliMUONLocalTriggerBoard* localBoard = fCrateManager->LocalBoard(nBoard);
      Int_t detElemId = triggerCircuit.DetElemId(iChamber+10, localBoard->GetName());//FIXME +/-10 (should be ok with new mapping)
        
        const AliMpVSegmentation* seg 
          = AliMpSegmentation::Instance()
          ->GetMpSegmentation(detElemId, AliMp::GetCathodType(iCath));  
        
        // loop over the 16 bits of pattern
        for (Int_t ibitxy = 0; ibitxy < 16; ++ibitxy) 
        {
          if ((pattern >> ibitxy) & 0x1) 
          {            
            // not quite sure about this
            Int_t offset = 0;
            if (iCath && localBoard->GetSwitch(6)) offset = -8;
            
            AliMpPad pad = seg->PadByLocation(AliMpIntPair(nBoard,ibitxy+offset),kTRUE);
                        
            if (!pad.IsValid()) 
            {
              AliWarning(Form("No pad for detElemId: %d, nboard %d, ibitxy: %d\n",
                              detElemId, nBoard, ibitxy));
              continue;
            }
            
            AliMUONVDigit* digit = digitStore.Add(detElemId,nBoard,ibitxy,iCath,AliMUONVDigitStore::kDeny);
            
            if (!digit)
            {
              AliError(Form("Could not add digit DE %04d LocalBoard %03d ibitxy %02d cath %d",
                            detElemId,nBoard,ibitxy,iCath));
              continue;
            }
            
            Int_t padX = pad.GetIndices().GetFirst();
            Int_t padY = pad.GetIndices().GetSecond();
            
            // fill digit
            digit->SetPadXY(padX,padY);
            digit->SetCharge(1.);
          }// xyPattern
        }// ibitxy
    }// cath
  } // ichamber
  
  return kTRUE;
} 
//____________________________________________________________________
void  
AliMUONDigitMaker::GetCrateName(Char_t* name, Int_t iDDL, Int_t iReg) const
{
  /// set crate name from DDL & reg number
  /// method same as in RawWriter, not so nice
  /// should be put in AliMUONTriggerCrateStore

      switch(iReg) {
      case 0:
      case 1:
	sprintf(name,"%d", iReg+1);
	break;
      case 2:
	strcpy(name, "2-3");
	break;
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
	sprintf(name,"%d", iReg);
	break;
      }

      // crate Right for first DDL
      if (iDDL == 0)
	strcat(name, "R");
      else 
	strcat(name, "L"); 
}
