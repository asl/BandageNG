#ifndef BANDAGE_GRAPH_ANNOTATION_HPP_
#define BANDAGE_GRAPH_ANNOTATION_HPP_

#include <QColor>
#include <QPainterPath>

#include "blast/blasthit.h"
#include "blast/blastquery.h"

struct Annotation {
    double widthMultiplier;
    QColor color;
    int64_t start;
    int64_t end;
    std::string text;

    static Annotation fromBlastHit(const BlastHit &blastHit) {
        return {
            1.0,
            blastHit.m_query->getColour(),
            blastHit.m_nodeStart,
            blastHit.m_nodeEnd,
            blastHit.m_query->getName().toStdString(),
        };
    }
};

#endif //BANDAGE_GRAPH_ANNOTATION_HPP_
