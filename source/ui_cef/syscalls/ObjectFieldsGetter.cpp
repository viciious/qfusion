#include "ObjectFieldsGetter.h"
#include "../CefStringBuilder.h"

ObjectFieldsGetter::ObjectFieldsGetter( CefRefPtr<CefV8Value> object_ )
	: object( object_ ) {

	std::vector<CefString> objectKeys;
	// Must always succeed
	assert( object->GetKeys( objectKeys ) );

	// CefString does not have an intrinsic hash, so a hash set is not going to be more efficient
	for( const auto &key: objectKeys ) {
		// Try skipping integer-based keys
		if( key.length() > 1 ) {
			auto ch = (char)key.c_str()[0];
			if( ch < '0' || ch > '9' ) {
				keySet.emplace( key );
			}
		}
	}
}

void ObjectFieldsGetter::SetTypeError( const CefString &keyName, const CefString &scope,
									   const CefString &expected, CefString &exception ) {
	CefStringBuilder s;
	s << "A field `" << keyName << "`";
	if( !scope.empty() ) {
		s << "in `" << scope << "`";
	}
	s << " is not " << expected;
	exception = s.ReleaseOwnership();
}

CefRefPtr<CefV8Value> ObjectFieldsGetter::GetFieldValue( const CefString &keyName,
														 CefString &exception,
														 const CefString &scope ) {
	// Check by name, do not trust object->GetValue() as it returns "undefined" values on failure
	if( !ContainsField( keyName ) ) {
		CefStringBuilder s;
		s << "Can't find a field by key `" << keyName << "`";
		if( !scope.empty() ) {
			s << " in `" << scope << "`";
		}
		exception = s.ReleaseOwnership();
		return nullptr;
	}

	return object->GetValue( keyName );
}

bool ObjectFieldsGetter::GetString( const CefString &keyName, CefString &result,
									CefString &exception, const CefString &scope ) {
	auto valueForKey( GetFieldValue( keyName, exception, scope ) );
	if( !valueForKey ) {
		return false;
	}

	if( !valueForKey->IsString() ) {
		SetTypeError( keyName, scope, "a string", exception );
		return false;
	}

	result = valueForKey->GetStringValue();
	return true;
}

bool ObjectFieldsGetter::GetBool( const CefString &keyName, bool *result, CefString &exception, const CefString &scope ) {
	auto valueForKey( GetFieldValue( keyName, exception, scope ) );
	if( !valueForKey ) {
		return false;
	}

	if( !valueForKey->IsBool() ) {
		SetTypeError( keyName, scope, "a boolean", exception );
		return false;
	}

	*result = valueForKey->GetBoolValue();
	return true;
}

bool ObjectFieldsGetter::GetDouble( const CefString &keyName, double *result,
									CefString &exception, const CefString &scope ) {
	auto valueForKey( GetFieldValue( keyName, exception, scope ) );
	if( !valueForKey ) {
		return false;
	}

	if( !valueForKey->IsDouble() ) {
		if( valueForKey->IsInt() ) {
			*result = valueForKey->GetIntValue();
			return true;
		}
		if( valueForKey->IsUInt() ) {
			*result = valueForKey->GetUIntValue();
			return true;
		}
		SetTypeError( keyName, scope, "a double (or just numeric)", exception );
		return false;
	}

	*result = valueForKey->GetDoubleValue();
	return true;
}

bool ObjectFieldsGetter::GetFloat( const CefString &keyName, float *result, CefString &exception, const CefString &scope ) {
	double tmp;
	if( !GetDouble( keyName, &tmp, exception, scope ) ) {
		return false;
	}

	volatile auto f = (float)tmp;
	if( (double)f == tmp ) {
		*result = f;
		return true;
	}

	CefStringBuilder s;
	s << "Precision loss while trying to convert double value " << tmp << " of `" << keyName << "`";
	if( !scope.empty() ) {
		s << " in `" << scope << "`";
	}
	s << " to float";

	exception = s.ReleaseOwnership().ToString();
	return false;
}

bool ObjectFieldsGetter::GetInt( const CefString &keyName, int *result, CefString &exception, const CefString &scope ) {
	auto valueForKey( GetFieldValue( keyName, exception, scope ) );
	if( !valueForKey ) {
		return false;
	}

	if( valueForKey->IsInt() ) {
		*result = valueForKey->GetIntValue();
		return true;
	}

	if( valueForKey->IsUInt() ) {
		unsigned value = valueForKey->GetUIntValue();
		if( value <= std::numeric_limits<uint32_t>::max() ) {
			*result = (int)value;
			return true;
		}
		exception = "Can't convert an unsigned value " + std::to_string( value ) + " to a signed one";
		return false;
	}

	if( valueForKey->IsDouble() ) {
		double value = valueForKey->GetDoubleValue();
		volatile auto v = (int)value;
		if( (double)v == value ) {
			*result = v;
			return true;
		}
		exception = "Can't convert a double value" + std::to_string( value ) + " to an signed integer";
		return false;
	}

	return false;
}


bool ObjectFieldsGetter::GetUInt( const CefString &keyName, unsigned *result, CefString &exception, const CefString &scope ) {
	auto valueForKey( GetFieldValue( keyName, exception, scope ) );
	if( !valueForKey ) {
		return false;
	}

	if( valueForKey->IsUInt() ) {
		*result = valueForKey->GetUIntValue();
		return true;
	}

	if( valueForKey->IsInt() ) {
		int value = valueForKey->GetIntValue();
		if( value >= 0 ) {
			*result = (unsigned)value;
			return true;
		}
		exception = "Can't convert an integer value " + std::to_string( value ) + " to unsigned one";
		return false;
	}

	if( valueForKey->IsDouble() ) {
		double value = valueForKey->GetDoubleValue();
		if( value >= 0 ) {
			volatile auto v = (unsigned)value;
			if( (double)v == value ) {
				*result = v;
				return true;
			}
		}
		exception = "Can't convert a double value" + std::to_string( value ) + " to an unsigned integer";
		return false;
	}

	return false;
}

bool ObjectFieldsGetter::GetArray( const CefString &keyName,
								   CefRefPtr<CefV8Value> &result,
								   CefString &exception,
								   const CefString &scope ) {
	auto valueForKey( GetFieldValue( keyName, exception, scope ) );
	if( !valueForKey ) {
		return false;
	}

	if( !valueForKey->IsArray() ) {
		SetTypeError( keyName, scope, "an array", exception );
		return false;
	}

	result = valueForKey;
	return true;
}

bool ObjectFieldsGetter::GetObject( const CefString &keyName,
									CefRefPtr<CefV8Value> &result,
									CefString &exception,
									const CefString &scope ) {
	auto valueForKey( GetFieldValue( keyName, exception, scope ) );
	if( !valueForKey ) {
		return false;
	}

	if( !valueForKey->IsObject() ) {
		SetTypeError( keyName, scope, "an object", exception );
		return false;
	}

	result = valueForKey;
	return true;
}

bool ObjectFieldsGetter::GetFloatVec( const CefString &keyName,
									  float *result,
									  int size,
									  CefString &exception,
									  const CefString &scope ) {
	CefRefPtr<CefV8Value> valueForKey;
	if( !GetArray( keyName, valueForKey, exception, scope ) ) {
		return false;
	}

	const int length = valueForKey->GetArrayLength();
	assert( size > 0 );
	if( length != size ) {
		CefStringBuilder s;
		s << "An array field for key `" << keyName << "` ";
		if( !scope.empty() ) {
			s << "in `" << scope << "` ";
		}
		s << "must have " << size << " elements, got " << length << " ones";
		exception = s.ReleaseOwnership();
		return false;
	}

	for( int i = 0; i < size; ++i ) {
		auto elemValue( valueForKey->GetValue( i ) );
		// TODO: Catch precision loss?
		if( elemValue->IsInt() ) {
			result[i] = elemValue->GetIntValue();
		} else if( elemValue->IsUInt() ) {
			result[i] = elemValue->GetUIntValue();
		} else if( elemValue->IsDouble() ) {
			result[i] = (float)elemValue->GetDoubleValue();
		} else {
			SetTypeError( "#" + std::to_string( i ), keyName, "a number", exception );
			return false;
		}
	}

	return true;
}
