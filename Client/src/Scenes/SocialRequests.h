#pragma once

#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Outgoing.h"
#include "UI/ChatCommands.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// Los pedidos a otro jugador de los comandos del chat (CL9 hito 7, fase 2; 06 § Lo social en pantalla): /trade
// (0x56c68b..0x56d623), /duelstart sin duelo (0x56d823..0x56e9ec) y /party (0x56f13f..0x570d47) de 0x56c440. Cada uno
// busca al otro: el elegido (0x6c8e30) o, sin él, el primero de los 400 que está enfrente del héroe. Cada celda que
// compara abre y cierra su entrada protegida (0x40b570 y 0x40b800, o lo mismo en línea), con los cuatro rand() de su
// cierre; el paquete va en C3, con los dos del relleno y del serial. *Estático*.
namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

namespace Client::Scenes
{
class WorldState;

struct SocialRequestContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    // Text.bmd: el 475 y el 476 (sprintf con el nombre), el 493 (la tienda personal) y el 257 (no es el líder).
    const Data::Text::TextTable& texts;
    // El cable (App::ServerSession). Vacío, el pedido que llega a mandarse es el error.
    std::function<void(Protocol::Outgoing)> send{};
};

// 0x69fd70 (el relleno) y el serial de un C3 (0x40ae60 con el cierre de 0x40b0a0 o de 0x40b3f0): sus dos rand() y el
// paquete al cable. Sin el cable, el error con `where`.
[[nodiscard]] Core::Expected<void, std::string> SendInC3(const SocialRequestContext& context,
                                                         Protocol::Outgoing outgoing, std::string_view where);

// El 36 al personaje `index` y el aviso 475 (0x56c7c8..0x56cdfc y 0x56cffe..0x56d60e de /trade; 0x4064d6..0x406aea del
// comando 1 de la ventana de comandos): con la tienda personal propia o ajena abiertas (0x5af560) o sin el mundo
// cargado (0x7d3a101), nada; si no, el C1 05 36 con su clave en C3 y el aviso armado con el nombre del primero vivo
// con esa clave (0x5110d0), con el 1.
[[nodiscard]] Core::Expected<UI::ChatCommandOutcome, std::string>
SendATradeRequest(const SocialRequestContext& context, std::size_t index, std::string_view where);

// El 40 al personaje `index` y el aviso 476 (0x56f6d1..0x56fd88 y 0x570638..0x570d22 de /party; 0x407352..0x40798d del
// comando 3): sin el mundo cargado (0x7d3a101), nada; si no, 0x78aba4c = su clave (MOVSX), el C1 05 40 en C3 y el
// aviso con el 1.
[[nodiscard]] Core::Expected<UI::ChatCommandOutcome, std::string>
SendAPartyRequest(const SocialRequestContext& context, std::size_t index, std::string_view where);

// 0x56f13f..0x56f1b0 y 0x4072df..0x407324: con party (0x78aba48 sobre 0), el primer lugar de 0x77f3cd0 no es el héroe
// (strcmp con +0x1d9).
[[nodiscard]] bool AnotherLeadsTheParty(const WorldState& world);

// 0x4061ef..0x40625f: el personaje está en la party (strcmp de su +0x1d9 con el nombre de cada uno de los 0x78aba48
// lugares de 0x77f3cd0, de 0x24 bytes).
[[nodiscard]] bool IsInTheParty(const WorldState& world, std::size_t index);

// 0x408101..0x40813b: strcmp del +0x1d9 del personaje con el nombre del rival del duelo (0x78abae0).
[[nodiscard]] bool IsTheDuelRival(const WorldState& world, std::size_t index);

// 0x4060c1..0x406169: la distancia en celdas del personaje al héroe publicado, leyendo las cuatro (x del otro, x del
// héroe, y del otro, y del héroe) cada una en su acceso, con los rand() de su cierre, siempre las cuatro.
struct CellDistance
{
    std::int32_t x = 0;
    std::int32_t y = 0;
};
[[nodiscard]] CellDistance CellDistanceToTheHero(WorldState& world, Render::Models::CrtRandom& random,
                                                 std::size_t index);

// El otro al que se le puede pedir algo (0x56c6ab..0x56c6d6 y sus copias): un jugador (+0x88 en 1) que no es el héroe
// publicado (0x743a170), del tipo 0x20a o con +0x3e3 distinto de 0. El elegido no mira si está vivo; el de enfrente,
// sí (el byte de +0).
[[nodiscard]] bool CanBeAsked(const WorldState& world, std::size_t index);

// 0x56c6dc..0x56c78a: la celda lógica del otro y la del héroe, cada x y cada y en su acceso (+0x41c del otro, la del
// héroe, +0x420 del otro y la del héroe): a lo sumo 1 de distancia en las dos. Con la x lejos no lee las y.
[[nodiscard]] bool NextToTheHero(WorldState& world, Render::Models::CrtRandom& random, std::size_t index);

// 0x56cf2f..0x56cfac: las direcciones de +0x24 (el _ftol de (ángulo + 22,5) / 45 + 1, & 0xff y por 8) del otro y del
// héroe, a 4 de distancia: se miran. Fuera del _ftol, el error.
[[nodiscard]] Core::Expected<bool, std::string> FacesTheHero(const WorldState& world, std::size_t index);

// 0x56c68b..0x56d623 (/trade con el nivel). El elegido, si está y se le puede pedir (si no, 1), o el primero de los
// 400 vivo, al lado y enfrente (sin ninguno, 1). Al lado de una tienda personal (0x451810), el aviso 493 con el 2 y 1.
// Con la tienda personal propia o ajena abierta (0x5af560) o sin el mundo cargado (0x7d3a101), 1. Si no, el C1 05 36
// con su clave y el aviso 475 con el nombre del primero vivo con esa clave (0x5110d0), con el 1, y 1.
[[nodiscard]] Core::Expected<UI::ChatCommandOutcome, std::string> RequestATrade(const SocialRequestContext& context);

// 0x56d823..0x56e9ec (/duelstart sin duelo): el elegido o el de enfrente, como /trade pero sin la tienda, sin
// 0x5af560 ni 0x7d3a101 y sin aviso, y el C1 0F AA con su clave y su nombre. Con o sin él, la cadena sigue
// (0x56e07c).
[[nodiscard]] Core::Expected<void, std::string> RequestADuel(const SocialRequestContext& context);

// 0x56f13f..0x570d47 (/party fuera de Chaos Castle): con party (0x78aba48 sobre 0) y otro líder (strcmp del primer
// lugar de 0x77f3cd0 con el nombre del héroe), el aviso 257 con el 1. Si no, el elegido o el de enfrente, como
// /trade pero sin la tienda ni 0x5af560; con el mundo cargado, 0x78aba4c = su clave (MOVSX), el C1 05 40 y el aviso
// 476. Devuelve 1 siempre.
[[nodiscard]] Core::Expected<UI::ChatCommandOutcome, std::string> RequestAParty(const SocialRequestContext& context);
} // namespace Client::Scenes
