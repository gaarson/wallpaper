// mode_grid.h
#ifndef MODE_GRID_H
#define MODE_GRID_H

#include "mode.h" // Включаем основной интерфейс

// Объявляем внешнюю константу, которая будет содержать
// указатели на функции реализации для режима сетки.
// Это позволит другим частям программы использовать этот режим,
// зная только этот заголовочный файл и сам интерфейс mode.h
extern const RenderModeInterface grid_mode_interface;

#endif // MODE_GRID_H
