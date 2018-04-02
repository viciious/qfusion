/*
Copyright (C) 2011 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "ui_precompiled.h"
#include "kernel/ui_common.h"
#include "kernel/ui_demoinfo.h"
#include "kernel/ui_utils.h"

namespace WSWUI
{

DemoInfo::DemoInfo() {
	setName( "" );
}

DemoInfo::DemoInfo( const char *name ) {
	setName( name );
}

DemoInfo & DemoInfo::operator =( const DemoInfo &other ) {
	this->directory = other.directory;
	this->time = other.time;
	this->isPlaying = other.isPlaying;
	this->isPaused = other.isPaused;
	this->hasMetaData = other.hasMetaData;
	this->metaData = other.metaData;
	return *this;
}

const bool DemoInfo::isValid( void ) const {
	return !name.empty();
}

void DemoInfo::setName( const std::string & newName ) {
	this->name = newName;
	this->directory.clear();
	this->hasMetaData = false;
	this->time = 0;
	this->isPlaying = this->isPaused = false;
	this->metaData.clear();
}

const std::string &DemoInfo::getName() const {
	return name;
}

const std::string DemoInfo::getFullPath() const {
	return directory + name;
}

const DemoMetaData &DemoInfo::getMetaData() {
	if( !hasMetaData ) {
		hasMetaData = true;
		readMetaData();
	}
	return metaData;
}

void DemoInfo::setDirectory( const std::string &directory_ ) {
	directory = directory_.empty() ? "" : directory_ + "/";
}

void DemoInfo::Play( void ) const {
	std::string playcmd = std::string( "demo \"" ) + getName() + "\"";
	trap::Cmd_ExecuteText( EXEC_APPEND, playcmd.c_str() );
}

void DemoInfo::Pause( void ) const {
	if( isPlaying ) {
		trap::Cmd_ExecuteText( EXEC_NOW, "demopause" );
	}
}

void DemoInfo::Stop( void ) const {
	if( isPlaying ) {
		trap::Cmd_ExecuteText( EXEC_APPEND, "disconnect" );
	}
}

void DemoInfo::Jump( unsigned int jtime ) const {
	std::string jumpcmd = std::string( "demojump \"" ) + WSWUI::toString( jtime ) + "\"";
	trap::Cmd_ExecuteText( EXEC_NOW, jumpcmd.c_str() );
}

void DemoInfo::readMetaData( void ) {
	const size_t meta_data_c_size = 16 * 1024;
	char meta_data_c_str[meta_data_c_size];

	std::string fullName = getFullPath();
	size_t meta_data_realsize = trap::CL_ReadDemoMetaData( fullName.c_str(), meta_data_c_str, meta_data_c_size );

	metaData.clear();

	const char *s, *key, *value;
	const char *end = meta_data_c_str + std::min( meta_data_c_size, meta_data_realsize );
	for( s = meta_data_c_str; s < end && *s; ) {
		key = s;
		value = key + strlen( key ) + 1;
		if( value >= end ) {
			// key without the value pair, EOF
			break;
		}

		metaData[key] = COM_RemoveColorTokens( value );
		s = value + strlen( value ) + 1;
	}
}

}
