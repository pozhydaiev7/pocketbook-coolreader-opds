/***************************************************************************
 *   CoolReader engine                                                     *
 *   Copyright (C) 2007,2011 Vadim Lopatin <coolreader.org@gmail.com>      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License           *
 *   as published by the Free Software Foundation; either version 2        *
 *   of the License, or (at your option) any later version.                *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the Free Software           *
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,            *
 *   MA 02110-1301, USA.                                                   *
 ***************************************************************************/

#ifndef __LVSTREAMFRAGMENT_H_INCLUDED__
#define __LVSTREAMFRAGMENT_H_INCLUDED__

#include "lvnamedstream.h"

class LVStreamFragment : public LVNamedStream
{
private:
    LVStreamRef m_stream;
    lvsize_t    m_start;
    lvsize_t    m_size;
    lvpos_t     m_pos;
public:
    LVStreamFragment( LVStreamRef stream, lvsize_t start, lvsize_t size )
        : m_stream(stream), m_start(start), m_size(size), m_pos(0)
    {
        m_mode = LVOM_READ;
    }
    virtual bool Eof()
    {
        return m_pos >= m_size;
    }
    virtual lvsize_t  GetSize()
    {
        return m_size;
    }

    virtual lverror_t Seek(lvoffset_t offset, lvseek_origin_t origin, lvpos_t* newPos)
    {
        lvpos_t base;
        if (origin == LVSEEK_SET) base = 0;
        else if (origin == LVSEEK_CUR) base = m_pos;
        else if (origin == LVSEEK_END) base = m_size;
        else return LVERR_FAIL;
        // Avoid negating the minimum signed offset or overflowing position arithmetic.
        lvpos_t target;
        if (offset < 0) {
            lvpos_t distance = static_cast<lvpos_t>(-(offset + 1)) + 1;
            if (distance > base) return LVERR_FAIL;
            target = base - distance;
        } else {
            if (base > m_size || static_cast<lvpos_t>(offset) > m_size - base)
                return LVERR_FAIL;
            target = base + offset;
        }
        if (target > m_size || m_start + target < m_start) return LVERR_FAIL;
        // Every read positions the backing stream; fragment seeking has no side effects.
        m_pos = target;
        if (newPos) *newPos = target;
        return LVERR_OK;
    }
    virtual lverror_t Write(const void*, lvsize_t, lvsize_t*)
    {
        return LVERR_NOTIMPL;
    }
    virtual lverror_t Read(void* buf, lvsize_t size, lvsize_t* pBytesRead)
    {
        if (pBytesRead) *pBytesRead = 0;
        if (m_pos > m_size || m_start + m_pos < m_start) return LVERR_FAIL;
        if (size > m_size - m_pos) size = m_size - m_pos;
        if (!size) return LVERR_OK;
        lvsize_t bytesRead = 0;
        lvpos_t p;
        lverror_t res = m_stream->Seek( m_pos+m_start, LVSEEK_SET, &p );
        if ( res!=LVERR_OK )
            return res;
        res = m_stream->Read( buf, size, &bytesRead );
        if (res == LVERR_OK)
            m_pos += bytesRead;
        if (pBytesRead)
            *pBytesRead = bytesRead;
        return res;
    }
    virtual lverror_t SetSize(lvsize_t)
    {
        return LVERR_NOTIMPL;
    }
};

#endif  // __LVSTREAMFRAGMENT_H_INCLUDED__
