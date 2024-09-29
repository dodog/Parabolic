#include "models/urlinfo.h"

namespace Nickvision::TubeConverter::Shared::Models
{
    UrlInfo::UrlInfo(const std::string& url, boost::json::object info, bool includeAutoGeneratedSubtitles, VideoCodec preferredVideoCodec)
        : m_url{ url },
        m_title{ info["title"].is_string() ? info["title"].as_string().c_str() : "" },
        m_isPlaylist{ false }
    {
        boost::json::array entries = info["entries"].is_array() ? info["entries"].as_array() : boost::json::array();
        if(!entries.empty())
        {
            m_isPlaylist = true;
            for(const boost::json::value& entry : entries)
            {
                if(!entry.is_object())
                {
                    continue;
                }
                boost::json::object obj = entry.as_object();
                obj["limit_characters"] = info["limit_characters"];
                m_media.push_back({ obj, includeAutoGeneratedSubtitles, preferredVideoCodec });
            }
        }
        else
        {
            m_media.push_back({ info, includeAutoGeneratedSubtitles, preferredVideoCodec });
        }
    }

    const std::string& UrlInfo::getUrl() const
    {
        return m_url;
    }

    const std::string& UrlInfo::getTitle() const
    {
        return m_title;
    }

    bool UrlInfo::isPlaylist() const
    {
        return m_isPlaylist;
    }

    size_t UrlInfo::count() const
    {
        return m_media.size();
    }

    Media& UrlInfo::get(const size_t index)
    {
        return m_media[index];
    }

    const Media& UrlInfo::get(const size_t index) const
    {
        return m_media[index];
    }

    Media& UrlInfo::operator[](const size_t index)
    {
        return m_media[index];
    }

    const Media& UrlInfo::operator[](const size_t index) const
    {
        return m_media[index];
    }
}