#pragma once
#include <opencv2/opencv.hpp>
#include <QObject>
#include <QtQmlIntegration/QtQmlIntegration>

// 检测框，包含中心点坐标(x,y)、宽高(w,h)、置信度(conf)、类别(cls) 
struct Box {
	Q_GADGET
		QML_ELEMENT
		Q_PROPERTY( float x READ getX )
		Q_PROPERTY( float y READ getY )
		Q_PROPERTY( float width READ getWidth )
		Q_PROPERTY( float height READ getHeight )
		Q_PROPERTY( float confidence READ getConf )
		Q_PROPERTY( int classId READ getCls )
public:
	float x, y, w, h, conf;	  // 中心点坐标(x,y)，宽高(w,h)
	int cls;

	Box() = default;
	Box( float x_, float y_, float w_, float h_, float conf_, int cls_ )
		: x( x_ ), y( y_ ), w( w_ ), h( h_ ), conf( conf_ ), cls( cls_ ) {
	}
	//  getter 供 QML 读取
	float getX() const { return x; }
	float getY() const { return y; }
	float getWidth() const { return w; }
	float getHeight() const { return h; }
	float getConf() const { return conf; }
	int getCls() const { return cls; }
	float getArea()const { return w * h; }

	static cv::Rect Box2Rect( const Box& box, int W, int H ) {
		int x = static_cast<int>(box.x - box.w / 2);
		int y = static_cast<int>(box.y - box.h / 2);
		int w = static_cast<int>(box.w);
		int h = static_cast<int>(box.h);
		// 防止越界
		x = std::max( 0, x );
		y = std::max( 0, y );
		w = std::min( w, W - x );	  // 向上取整
		h = std::min( h, H - y );
		return cv::Rect( x, y, w, h );
	}
	static Box Rect2Box( const cv::Rect& rect, float conf, int cls, int W, int H ) {
		float x = static_cast<float>(rect.x + rect.width / 2);
		float y = static_cast<float>(rect.y + rect.height / 2);
		float w = static_cast<float>(rect.width);
		float h = static_cast<float>(rect.height);
		// 防止越界
		x = std::max( 0.0f, std::min( x, static_cast<float>(W) ) );
		y = std::max( 0.0f, std::min( y, static_cast<float>(H) ) );
		w = std::max( 0.0f, std::min( w, static_cast<float>(W - x) ) );
		h = std::max( 0.0f, std::min( h, static_cast<float>(H - y) ) );

		return Box{ x, y, w, h, conf, cls };
	}
};
