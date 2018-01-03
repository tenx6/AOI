////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 SafeNet, Inc. All rights reserved.
//
// Dog(R) is a registered trademark of SafeNet, Inc. 
//
//
////////////////////////////////////////////////////////////////////
#include "dog_api_cpp_.h"
#include <string.h>

////////////////////////////////////////////////////////////////////
// Construction/Destruction
////////////////////////////////////////////////////////////////////

CDogHandle::CDogHandle()
{
    clear();
}

CDogHandle::~CDogHandle()
{
    clear();
}

////////////////////////////////////////////////////////////////////
// Implementation
////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////
//! Equality operator
////////////////////////////////////////////////////////////////////
bool CDogHandle::operator==(const CDogHandle& other) const
{
    return (0 == memcmp(this, &other, sizeof(*this)));
}

////////////////////////////////////////////////////////////////////
//! Inequality operator
////////////////////////////////////////////////////////////////////
bool CDogHandle::operator!=(const CDogHandle& other) const
{
    return !this->operator==(other);
}

////////////////////////////////////////////////////////////////////
//! Clears the handle
////////////////////////////////////////////////////////////////////
void CDogHandle::clear()
{
    memset(this, 0x00, sizeof(*this));
}

////////////////////////////////////////////////////////////////////
//! Determines if the handle is valid
////////////////////////////////////////////////////////////////////
bool CDogHandle::isNull() const
{
    return (0 == m_ulIndex);
}