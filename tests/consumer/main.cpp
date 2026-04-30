#include <qmlscrubplayer.h>

int main()
{
    QmlScrubPlayer player;
    return player.duration() == 0 ? 0 : 1;
}
