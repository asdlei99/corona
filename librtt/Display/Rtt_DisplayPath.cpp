//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_DisplayPath.h"

#include "Rtt_Matrix.h"
#include "Display/Rtt_DisplayObject.h"
#include "Display/Rtt_VertexCache.h"
#include "Renderer/Rtt_Geometry_Renderer.h"
#include "Rtt_LuaUserdataProxy.h"

#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

void
DisplayPath::UpdateGeometry( Geometry& dst, const VertexCache& src, const Matrix& srcToDstSpace, U32 flags, Array<U16> *indices )
{
	if ( 0 == flags ) { return; }

	const ArrayVertex2& vertices = src.Vertices();
	const ArrayVertex2& texVertices = src.TexVertices();
	U32 numVertices = vertices.Length();

	U32 numIndices = indices==NULL?0:indices->Length();
	if ( dst.GetVerticesAllocated() < numVertices || dst.GetIndicesAllocated() < numIndices)
	{
		dst.Resize( numVertices, numIndices, false );
	}
	Geometry::Vertex *dstVertices = dst.GetVertexData();

	bool updateVertices = ( flags & kVerticesMask );
	bool updateTexture = ( flags & kTexVerticesMask );

	Rtt_ASSERT( ! updateTexture || ( vertices.Length() == texVertices.Length() ) );

    const ArrayFloat * floatArray = src.ExtraFloatArray( ZKey() );
    const ArrayIndex * indexArray = src.ExtraIndexArray( IndicesKey() );

    std::vector< float > temp;
    const float zero = 0.f, * zsource;
    size_t step = 0U;

    if (floatArray)
    {
        if (indexArray)
        {
            for (S32 i = 0; i < indexArray->Length(); ++i)
            {
                U16 index = (*indexArray)[i];

                temp.push_back( (*floatArray)[index] );
            }

            zsource = temp.data();
        }

        else
        {
            zsource = floatArray->ReadAccess();
        }

        ++step;
    }

    else
    {
        zsource = &zero;
        step = 0U;
    }

    for ( U32 i = 0, iMax = vertices.Length(); i < iMax; i++, zsource += step )
	{
		Rtt_ASSERT( i < dst.GetVerticesAllocated() );

		Geometry::Vertex& dst = dstVertices[i];

		if ( updateVertices )
		{
			Vertex2 v = vertices[i];
			srcToDstSpace.Apply( v );

			dst.x = v.x;
			dst.y = v.y;
            dst.z = *zsource;
		}

		if ( updateTexture )
		{
			dst.u = texVertices[i].x;
			dst.v = texVertices[i].y;
			dst.q = 1.f;
		}
	}

	dst.SetVerticesUsed( numVertices );
	
	if(flags & kIndicesMask)
	{
		if(indices)
		{
			const U16* indicesData = indices->ReadAccess();
			U16* dstData = dst.GetIndexData();
			numIndices = indices->Length();
			for (U32 i=0; i<numIndices; i++)
			{
				dstData[i] = indicesData[i];
			}
			
			dst.Invalidate();
		}
		dst.SetIndicesUsed(numIndices);
	}
}

const void *
DisplayPath::ZKey()
{
    static int key;

    return &key;
}

const void *
DisplayPath::IndicesKey()
{
    static int key;

    return &key;
}

// ----------------------------------------------------------------------------

DisplayPath::DisplayPath()
:	fObserver( NULL ),
	fAdapter( NULL ),
	fProxy( NULL )
{
}

DisplayPath::~DisplayPath()
{
	if ( fProxy )
	{
		GetObserver()->QueueRelease( fProxy ); // Release native ref to Lua-side proxy
		fProxy->DetachUserdata(); // Notify proxy that object is invalid
	}
}

void
DisplayPath::PushProxy( lua_State *L ) const
{
	if ( ! fProxy )
	{
		fProxy = LuaUserdataProxy::New( L, const_cast< Self * >( this ) );
		fProxy->SetAdapter( GetAdapter() );
	}

	fProxy->Push( L );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

