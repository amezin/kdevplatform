/***************************************************************************
 *   Copyright 1999-2001 Bernd Gehrmann <bernd@kdevelop.org>               *
 *   Copyright 2007 Dukju Ahn <dukjuahn@gmail.com>                         *
 *   Copyright (C) 2012  Morten Danielsen Volden mvolden2@gmail.com        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "outputformats.h"

#include <KLocalizedString>

namespace KDevelop
{

ErrorFormat::ErrorFormat( const QString& regExp, int file, int line, int text, int column )
    : expression( regExp )
    , fileGroup( file )
    , lineGroup( line )
    , columnGroup( column )
    , textGroup( text )
{}

ErrorFormat::ErrorFormat( const QString& regExp, int file, int line, int text, const QString& comp, int column )
    : expression( regExp )
    , fileGroup( file )
    , lineGroup( line )
    , columnGroup( column )
    , textGroup( text )
    , compiler( comp )
{}

ActionFormat::ActionFormat( const KLocalizedString& label, const QString& _tool, const QString& regExp, int file )
    : label( label )
    , expression( regExp )
    , tool( _tool )
    , toolGroup(-1)
    , fileGroup( file )
{
}

ActionFormat::ActionFormat( const KLocalizedString& label, int tool, int file, const QString& regExp)
    : label( label )
    , expression( regExp )
    , toolGroup( tool )
    , fileGroup( file )
{
}

}

