#ifndef UI_CEF_OBJECT_FIELDS_GETTER_H
#define UI_CEF_OBJECT_FIELDS_GETTER_H

#include "include/cef_v8.h"
#include "set"

#include "../../gameshared/q_math.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

class ObjectFieldsGetter {
	CefRefPtr<CefV8Value> object;
	// Its more reliable to check against these keys and do not be confused by "undefined" values.
	// This helps also for a-priori field precense checks without actual values retrieval.
	std::set<CefString> keySet;

	void SetTypeError( const CefString &keyName, const CefString &scope, const CefString &expected, CefString &exception );

public:
	bool ContainsField( const CefString &name ) const {
		return keySet.find( name ) != keySet.end();
	}

	CefRefPtr<CefV8Value> GetFieldValue( const CefString &keyName,
										 CefString &exception,
										 const CefString &scope = CefString() );

	explicit ObjectFieldsGetter( CefRefPtr<CefV8Value> object_ );

	bool GetObject( const CefString &keyName, CefRefPtr<CefV8Value> &result,
					CefString &exception, const CefString &scope = CefString() );
	bool GetArray( const CefString &keyName, CefRefPtr<CefV8Value> &result,
				   CefString &exception, const CefString &scope = CefString() );

	bool GetString( const CefString &keyName, CefString &result, CefString &exception, const CefString &scope = CefString() );
	bool GetBool( const CefString &keyName, bool *result, CefString &exception, const CefString &scope = CefString() );
	bool GetInt( const CefString &keyName, int *result, CefString &exception, const CefString &scope = CefString() );
	bool GetUInt( const CefString &keyName, unsigned *result, CefString &exception, const CefString &scope = CefString() );
	bool GetDouble( const CefString &keyName, double *result, CefString &exception, const CefString &scope = CefString() );
	bool GetFloat( const CefString &keyName, float *result, CefString &exception, const CefString &scope = CefString() );

	bool GetFloatVec( const CefString &keyName, float *result, int size, CefString &exception, const CefString &scope );

	bool GetVec3( const CefString &keyName, vec3_t result, CefString &exception, const CefString &scope = CefString() ) {
		return GetFloatVec( keyName, result, 3, exception, scope );
	}
	bool GetVec2( const CefString &keyName, vec3_t result, CefString &exception, const CefString &scope = CefString() ) {
		return GetFloatVec( keyName, result, 2, exception, scope );
	}
};

#endif
