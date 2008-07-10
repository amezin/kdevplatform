/* This file is part of KDevelop
    Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef DOCUMENTRANGEOBJECT_P
#define DOCUMENTRANGEOBJECT_P

#include "documentrangeobject.h"
#include <ktexteditor/smartrange.h>
#include "simplerange.h"
#include "appendedlist.h"

namespace KDevelop
{

///Contains data that is not stored to disk
class DocumentRangeObjectDynamicPrivate {
  public:
    DocumentRangeObjectDynamicPrivate();
    mutable KTextEditor::SmartRange* m_smartRange; //Mutable for synchronization
    QMutex* m_smartMutex;
    DocumentRangeObject::RangeOwning m_ownsRange;
};

}

#endif
