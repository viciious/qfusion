/*
 * ui_modelview.cpp
 *
 *  Created on: 29.6.2011
 *      Author: hc
 */

#include "ui_precompiled.h"
#include "kernel/ui_common.h"
#include "kernel/ui_main.h"
#include "kernel/ui_boneposes.h"
#include "widgets/ui_widgets.h"
#include "../gameshared/q_shared.h"

#define MODELVIEW_EPSILON       1.0f

namespace WSWUI
{

using namespace Rocket::Core;

// forward-declare the instancer for keyselects
class UI_ModelviewWidgetInstancer;

class UI_ModelviewWidget : public Element, EventListener
{
private:
	entity_t entity;
	refdef_t refdef;
	vec3_t baseangles;
	vec3_t angles;
	vec3_t anglespeed;
	int64_t time;
	bool AutoRotationCenter;
	bool Initialized;
	bool RecomputePosition;
	UI_BonePoses *BonePoses;
	cgs_skeleton_t *skel;
	String modelName;
	String skinName;
	float fov_x, fov_y;

public:
	UI_ModelviewWidget( const String &tag )
		: Element( tag ),
		time( 0 ), AutoRotationCenter( false ), Initialized( false ), RecomputePosition( false ),
		BonePoses( NULL ), skel( NULL ), modelName( "" ), skinName( "" ),
		fov_x( 30.0f ), fov_y( 0.0f ) {
		memset( &entity, 0, sizeof( entity ) );
		memset( &refdef, 0, sizeof( refdef ) );
		entity.renderfx = RF_NOSHADOW | RF_FORCENOLOD | RF_MINLIGHT;
		entity.frame = entity.oldframe = 1;
		refdef.rdflags = RDF_NOWORLDMODEL;
		refdef.areabits = 0;
		refdef.minLight = 0.7;
		Matrix3_Copy( axis_identity, refdef.viewaxis );

		// Some default values
		VectorSet( baseangles, 0, 0, 0 );
		VectorSet( anglespeed, 0, 0, 0 );
		entity.scale = 1.0f;
		entity.outlineHeight = 0.3f;
		Vector4Set( entity.outlineRGBA, 64, 64, 64, 255 );
		Vector4Set( entity.shaderRGBA, 255, 255, 255, 255 );
	}

	virtual void OnRender() {
		Element::OnRender();

		if( !Initialized ) {
			Initialize();
			Initialized = true;
		}

		if( RecomputePosition ) {
			ComputePosition();
			RecomputePosition = false;
		}

		if( !entity.model ) {
			return;
		}

		int64_t curtime = UI_Main::Get()->getRefreshState().time;
		float deltatime = curtime - time;

		refdef.time = curtime;

		for( int i = 0; i < 3; ++i )
			angles[i] = anglemod( angles[i] + deltatime * anglespeed[i] / 1000.0f );

		AnglesToAxis( angles, entity.axis );

		if( AutoRotationCenter ) {
			// Update origin to make the rotation centered into viewport
			vec3_t mins, maxs;
			trap::R_ModelBounds( entity.model, mins, maxs );
			vec3_t buf;
			buf[0] = -0.5 * ( mins[0] + maxs[0] );
			buf[1] = -0.5 * ( mins[1] + maxs[1] );
			buf[2] = -0.5 * ( mins[2] + maxs[2] );

			// Just keep the x component
			float xoffset = entity.origin[0];
			mat3_t localAxis;
			Matrix3_Transpose( entity.axis, localAxis );
			Matrix3_TransformVector( localAxis, buf, entity.origin );
			entity.origin[0] = xoffset;
		}

		Rocket::Core::Vector2f offset = GetAbsoluteOffset( Rocket::Core::Box::CONTENT );
		refdef.x = offset.x;
		refdef.y = offset.y;

		// clip scissor region to parent
		int scissor_x, scissor_y, scissor_w, scissor_h;
		trap::R_GetScissor( &scissor_x, &scissor_y, &scissor_w, &scissor_h );
		refdef.scissor_x = scissor_x;
		refdef.scissor_y = scissor_y;
		refdef.scissor_width = scissor_w;
		refdef.scissor_height = scissor_h;

		trap::R_ClearScene();

		trap::R_AddEntityToScene( &entity );

		trap::R_RenderScene( &refdef );

		trap::R_Scissor( scissor_x, scissor_y, scissor_w, scissor_h );

		// TODO: Should this be done here or in ComputePosition?
		BonePoses->ResetTemporaryBoneposesCache();
		time = curtime;
	}

	virtual void OnPropertyChange( const Rocket::Core::PropertyNameList& changed_properties ) {
		Element::OnPropertyChange( changed_properties );

		for( Rocket::Core::PropertyNameList::const_iterator it = changed_properties.begin(); it != changed_properties.end(); ++it ) {
			if( *it == "model-modelpath" ) {
				modelName = GetProperty( *it )->Get<Rocket::Core::String>();
				Initialized = false;
			} else if( *it == "model-skinpath" && GetProperty( *it )->Get<Rocket::Core::String>().Length() > 0 ) {
				skinName = GetProperty( *it )->Get<Rocket::Core::String>();
				Initialized = false;
			} else if( *it == "model-scale" ) {
				entity.scale = GetProperty( *it )->Get<float>();
			} else if( *it == "model-outline-height" ) {
				entity.outlineHeight = GetProperty( *it )->Get<float>();
			} else if( *it == "model-outline-color" ) {
				Rocket::Core::Colourb color = GetProperty( *it )->Get<Rocket::Core::Colourb>();
				Vector4Set( entity.outlineRGBA, color.red, color.green, color.blue, color.alpha );
			} else if( *it == "model-shader-color" ) {
				Rocket::Core::Colourb color = GetProperty( *it )->Get<Rocket::Core::Colourb>();
				int shaderColor = COM_ValidatePlayerColor( COLOR_RGB( color.red, color.green, color.blue ) );
				Vector4Set( entity.shaderRGBA, COLOR_R( shaderColor ), COLOR_G( shaderColor ), COLOR_B( shaderColor ), color.alpha );
			} else if( *it == "model-fov-x" ) {
				const Property *prop = GetProperty( *it );
				if( prop->unit == Property::KEYWORD ) {
					fov_x = 0.0f;
				} else {
					fov_x = prop->Get<float>();
					clamp( fov_x, 1.0f, 179.0f );
				}
				RecomputePosition = true;
			} else if( *it == "model-fov-y" ) {
				const Property *prop = GetProperty( *it );
				if( prop->unit == Property::KEYWORD ) {
					fov_y = 0.0f;
				} else {
					fov_y = prop->Get<float>();
					clamp( fov_y, 1.0f, 179.0f );
				}
				RecomputePosition = true;
			} else if( *it == "model-rotation-pitch" ) {
				baseangles[0] = GetProperty( *it )->Get<float>();
				RecomputePosition = true;
			} else if( *it == "model-rotation-yaw" ) {
				baseangles[1] = GetProperty( *it )->Get<float>();
				RecomputePosition = true;
			} else if( *it == "model-rotation-roll" ) {
				baseangles[2] = GetProperty( *it )->Get<float>();
				RecomputePosition = true;
			} else if( *it == "model-rotation-speed-pitch" ) {
				anglespeed[0] = GetProperty( *it )->Get<float>();
			} else if( *it == "model-rotation-speed-yaw" ) {
				anglespeed[1] = GetProperty( *it )->Get<float>();
			} else if( *it == "model-rotation-speed-roll" ) {
				anglespeed[2] = GetProperty( *it )->Get<float>();
			} else if( *it == "model-rotation-autocenter" ) {
				AutoRotationCenter = ( GetProperty( *it )->Get<Rocket::Core::String>().ToLower() == "true" );
			}
		}

		if( std::fabs( refdef.width - GetClientWidth() ) >= MODELVIEW_EPSILON || std::fabs( refdef.height - GetClientHeight() ) >= MODELVIEW_EPSILON ) {
			RecomputePosition = true;
		}

		if( ( refdef.x - GetAbsoluteLeft() + GetClientLeft() ) >= MODELVIEW_EPSILON || ( refdef.y - GetAbsoluteTop() + GetClientTop() ) >= MODELVIEW_EPSILON ) {
			RecomputePosition = true;
		}
	}

	// Called when the element is added into a hierarchy.
	void OnChildAdd( Element* element ) {
		Element::OnChildAdd( element );

		if( element == this ) {
			Element *document = GetOwnerDocument();
			if( document == NULL ) {
				return;
			}
			document->AddEventListener( "invalidate", this );
		}
	}

	// Called when the element is removed from a hierarchy.
	void OnChildRemove( Element* element ) {
		Element::OnChildRemove( element );

		if( element == this ) {
			Element *document = GetOwnerDocument();
			if( document == NULL ) {
				return;
			}
			document->RemoveEventListener( "invalidate", this );
		}
	}

	// Called for every event sent to this element or one of its descendants.
	void ProcessEvent( Rocket::Core::Event& evt ) {
		if( evt == "invalidate" ) {
			Initialized = false;
			if( BonePoses ) {
				__delete__( BonePoses );
				BonePoses = NULL;
			}
		}
	}

	virtual ~UI_ModelviewWidget() {
		if( BonePoses ) {
			__delete__( BonePoses );
		}
	}

private:
	void Initialize() {
		BonePoses = __new__( UI_BonePoses )();
		RecomputePosition = true;

		if( modelName.Empty() ) {
			entity.model = NULL;
			return;
		}

		entity.model = trap::R_RegisterModel( modelName.CString() );
		entity.customSkin = trap::R_RegisterSkinFile( skinName.CString() );
	}

	void ComputePosition() {
		if( !entity.model ) {
			return;
		}

		// refdef setup
		Rocket::Core::Vector2f box = GetBox().GetSize( Rocket::Core::Box::CONTENT );
		refdef.x = 0;
		refdef.y = 0;
		refdef.width = box.x;
		refdef.height = box.y;

		refdef.fov_x = fov_x;
		refdef.fov_y = fov_y;
		if( !refdef.fov_x && !refdef.fov_y ) {
			refdef.fov_x = 30.0f;
		}
		if( !refdef.fov_x ) {
			refdef.fov_x = CalcFov( refdef.fov_y, refdef.height, refdef.width );
		} else if( !refdef.fov_y ) {
			refdef.fov_y = CalcFov( refdef.fov_x, refdef.width, refdef.height );
		}

		skel = NULL;
		if( trap::R_SkeletalGetNumBones( entity.model, NULL ) ) {
			skel = BonePoses->SkeletonForModel( entity.model );
			BonePoses->SetBoneposesForTemporaryEntity( &entity );
		}

		// entity setup
		// Set origin to fit the viewport according to initial rotation
		//trap::R_FitModelPositionInViewport( entity.model, baseangles, refdef.fov_x, refdef.fov_y, entity.origin);

		vec3_t mins, maxs;
		trap::R_ModelFrameBounds( entity.model, entity.frame, mins, maxs );

		entity.origin[0] = 0.5 * ( maxs[2] - mins[2] ) * ( 1.0 / 0.220 );
		entity.origin[1] = 0.5 * ( mins[1] + maxs[1] );
		entity.origin[2] = -0.5 * ( mins[2] + maxs[2] );

		VectorCopy( entity.origin, entity.origin2 );
		VectorCopy( baseangles, angles );
	}
};

//==============================================================

class UI_ModelviewWidgetInstancer : public ElementInstancer
{
public:
	UI_ModelviewWidgetInstancer() : ElementInstancer() {
		StyleSheetSpecification::RegisterProperty( "model-modelpath", "", false ).AddParser( "string" );
		StyleSheetSpecification::RegisterProperty( "model-skinpath", "", false ).AddParser( "string" );
		StyleSheetSpecification::RegisterProperty( "model-fov-x", "30", false ).AddParser( "keyword", "auto" ).AddParser( "number" );
		StyleSheetSpecification::RegisterProperty( "model-fov-y", "auto", false ).AddParser( "keyword", "auto" ).AddParser( "number" );
		StyleSheetSpecification::RegisterProperty( "model-scale", "1", false ).AddParser( "number" );
		StyleSheetSpecification::RegisterProperty( "model-outline-height", "0.3", false ).AddParser( "number" ); // DEFAULT_OUTLINE_HEIGHT
		StyleSheetSpecification::RegisterProperty( "model-outline-color", "#404040FF", false ).AddParser( "color" );
		StyleSheetSpecification::RegisterProperty( "model-shader-color", "#FFFFFFFF", false ).AddParser( "color" );
		StyleSheetSpecification::RegisterProperty( "model-rotation-pitch", "0", false ).AddParser( "number" );
		StyleSheetSpecification::RegisterProperty( "model-rotation-yaw", "0", false ).AddParser( "number" );
		StyleSheetSpecification::RegisterProperty( "model-rotation-roll", "0", false ).AddParser( "number" );
		StyleSheetSpecification::RegisterProperty( "model-rotation-speed-pitch", "0", false ).AddParser( "number" );
		StyleSheetSpecification::RegisterProperty( "model-rotation-speed-yaw", "0", false ).AddParser( "number" );
		StyleSheetSpecification::RegisterProperty( "model-rotation-speed-roll", "0", false ).AddParser( "number" );
	}

	// Rocket overrides
	virtual Element *InstanceElement( Element *parent, const String &tag, const XMLAttributes &attr ) {
		UI_ModelviewWidget *modelview = __new__( UI_ModelviewWidget )( tag );
		UI_Main::Get()->getRocket()->registerElementDefaults( modelview );
		return modelview;
	}

	virtual void ReleaseElement( Element *element ) {
		// then delete
		__delete__( element );
	}

	virtual void Release() {
		__delete__( this );
	}

private:
};

//============================================

ElementInstancer *GetModelviewInstancer( void ) {
	ElementInstancer *instancer = __new__( UI_ModelviewWidgetInstancer )();

	// instancer->RemoveReference();
	return instancer;
}

}
