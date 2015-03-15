//===-- MICmdArgValNumber.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//++
// File:        MICmdArgValNumber.cpp
//
// Overview:    CMICmdArgValNumber implementation.
//
// Environment: Compilers:  Visual C++ 12.
//                          gcc (Ubuntu/Linaro 4.8.1-10ubuntu9) 4.8.1
//              Libraries:  See MIReadmetxt.
//
// Copyright:   None.
//--

// In-house headers:
#include "MICmdArgValNumber.h"
#include "MICmdArgContext.h"

//++ ------------------------------------------------------------------------------------
// Details: CMICmdArgValNumber constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValNumber::CMICmdArgValNumber(void)
    : m_nNumber(0)
{
}

//++ ------------------------------------------------------------------------------------
// Details: CMICmdArgValNumber constructor.
// Type:    Method.
// Args:    vrArgName       - (R) Argument's name to search by.
//          vbMandatory     - (R) True = Yes must be present, false = optional argument.
//          vbHandleByCmd   - (R) True = Command processes *this option, false = not handled.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValNumber::CMICmdArgValNumber(const CMIUtilString &vrArgName, const bool vbMandatory, const bool vbHandleByCmd)
    : CMICmdArgValBaseTemplate(vrArgName, vbMandatory, vbHandleByCmd)
    , m_nNumber(0)
{
}

//++ ------------------------------------------------------------------------------------
// Details: CMICmdArgValNumber destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValNumber::~CMICmdArgValNumber(void)
{
}

//++ ------------------------------------------------------------------------------------
// Details: Parse the command's argument options string and try to extract the value *this
//          argument is looking for.
// Type:    Overridden.
// Args:    vwArgContext    - (RW) The command's argument options string.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmdArgValNumber::Validate(CMICmdArgContext &vwArgContext)
{
    if (vwArgContext.IsEmpty())
        return MIstatus::success;

    if (vwArgContext.GetNumberArgsPresent() == 1)
    {
        const CMIUtilString &rArg(vwArgContext.GetArgsLeftToParse());
        if (IsArgNumber(rArg) && ExtractNumber(rArg))
        {
            m_bFound = true;
            m_bValid = true;
            m_argValue = GetNumber();
            vwArgContext.RemoveArg(rArg);
            return MIstatus::success;
        }
        else
            return MIstatus::failure;
    }

    // More than one option...
    const CMIUtilString::VecString_t vecOptions(vwArgContext.GetArgs());
    CMIUtilString::VecString_t::const_iterator it = vecOptions.begin();
    while (it != vecOptions.end())
    {
        const CMIUtilString &rArg(*it);
        if (IsArgNumber(rArg) && ExtractNumber(rArg))
        {
            m_bFound = true;

            if (vwArgContext.RemoveArg(rArg))
            {
                m_bValid = true;
                m_argValue = GetNumber();
                return MIstatus::success;
            }
            else
                return MIstatus::failure;
        }

        // Next
        ++it;
    }

    return MIstatus::failure;
}

//++ ------------------------------------------------------------------------------------
// Details: Examine the string and determine if it is a valid string type argument.
// Type:    Method.
// Args:    vrTxt   - (R) Some text.
// Return:  bool    - True = yes valid arg, false = no.
// Throws:  None.
//--
bool
CMICmdArgValNumber::IsArgNumber(const CMIUtilString &vrTxt) const
{
    // Look for --someLongOption
    if (std::string::npos != vrTxt.find("--"))
        return false;

    return vrTxt.IsNumber();
}

//++ ------------------------------------------------------------------------------------
// Details: Extract the thread group number from the thread group argument.
// Type:    Method.
// Args:    vrTxt   - (R) Some text.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmdArgValNumber::ExtractNumber(const CMIUtilString &vrTxt)
{
    MIint64 nNumber = 0;
    bool bOk = vrTxt.ExtractNumber(nNumber);
    if (bOk)
    {
        m_nNumber = static_cast<MIint>(nNumber);
    }

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Retrieve the thread group ID found in the argument.
// Type:    Method.
// Args:    None.
// Return:  MIuint - Thread group ID.
// Throws:  None.
//--
MIint64
CMICmdArgValNumber::GetNumber(void) const
{
    return m_nNumber;
}
