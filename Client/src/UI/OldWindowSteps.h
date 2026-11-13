#pragma once

#include "Core/Expected.h"
#include "UI/FriendModel.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindows.h"

#include <cstdint>
#include <string>
#include <string_view>

// Lo que comparten UI/OldWindows.cpp (el administrador, el marco y la caja) y UI/FriendWindows.cpp (la ventana de
// amigos, sus pestañas y la caja de agregar): los métodos de la ventana base y del marco, y la vtable de las clases de
// los amigos. No es para fuera de UI.
namespace Client::UI::Steps
{
// 0x45a210 (con el id de 0x459ef0), 0x45ad80 y 0x491ca0.
[[nodiscard]] OldWindowBase MakeTheBase(OldWindowManager& manager);
[[nodiscard]] OldButton MakeTheButton(OldWindowManager& manager);
[[nodiscard]] OldWindow MakeTheFrame(OldWindowManager& manager);

// Ni oculta (3) ni minimizada (5).
[[nodiscard]] bool IsShowing(std::int32_t state);

// 0x459f00 con el mouse del paso.
[[nodiscard]] bool MouseIn(const OldWindowStepView& view, std::int32_t x, std::int32_t y, std::int32_t width,
                           std::int32_t height, std::int32_t mode);

// La vtable + 0x3c..+0x48: el área de adentro. El marco y sus derivadas (0x48f890..0x48f8e0) le sacan el borde y la
// barra; las pestañas de la de amigos (0x48fb40..0x48fb70: la x en 0x48fb40, la y en 0x48fb50, el ancho y el alto),
// nada.
[[nodiscard]] std::int32_t ClientX(const OldWindow& window, std::int32_t offset);
[[nodiscard]] std::int32_t ClientY(const OldWindow& window, std::int32_t offset);
[[nodiscard]] std::int32_t ClientWidth(const OldWindow& window);
[[nodiscard]] std::int32_t ClientHeight(const OldWindow& window);

// 0x45abc0.
void PlaceAgainstTheParent(OldWindowManager& manager, OldWindowBase& base);

// 0x45aa80 desde la cola hasta el rectángulo (ver OldWindows.cpp).
[[nodiscard]] bool ClaimTheMouse(OldWindowManager& manager, const OldWindowBase& base, const OldWindowGlobals& globals,
                                 const OldWindowStepView& view);

// 0x468d50, la vtable + 0x20 del EDIT, y 0x45aa80 sobre un EDIT (ver OldWindows.cpp).
void StepTheEditMouse(OldWindowManager& manager, OldEdit& edit, const OldWindowGlobals& globals,
                      const OldWindowStepView& view);
void StepTheEdit(OldWindowManager& manager, OldEdit& edit, std::int32_t arg, const OldWindowGlobals& globals,
                 const OldWindowStepView& view);

// 0x45aa80 sobre un botón y 0x45aa50 sobre un botón.
void StepTheButton(OldWindowManager& manager, OldButton& button, std::int32_t arg, const OldWindowGlobals& globals,
                   const OldWindowStepView& view);
void PostAndSettle(OldWindowManager& manager, OldButton& button, std::int32_t type);

// 0x45aa80 sobre una ventana (la vtable + 0x18) y 0x45aa50 (el mensaje y el paso con 1).
[[nodiscard]] Core::Expected<void, std::string> StepTheWindow(OldWindowManager& manager, OldWindow& window,
                                                              std::int32_t arg, const OldWindowGlobals& globals,
                                                              const OldWindowStepView& view);
[[nodiscard]] Core::Expected<void, std::string> PostAndStep(OldWindowManager& manager, OldWindow& window,
                                                            std::int32_t type, const OldWindowGlobals& globals,
                                                            const OldWindowStepView& view);

// La vtable + 0x30 de cualquiera.
[[nodiscard]] Core::Expected<void, std::string> SettleTheWindow(OldWindowManager& manager, OldWindow& window,
                                                                const OldWindowGlobals& globals,
                                                                const OldWindowStepView& view);

// 0x490670(id, mostrar).
void ShowTheWindow(OldWindowManager& manager, std::int32_t id, bool show);

// La ventana de amigos, sus pestañas y la caja de agregar (UI/FriendWindows.cpp).
[[nodiscard]] bool IsAFriendsKind(OldWindowKind kind);
// La vtable + 0x28 (0x4a2be0, 0x49d0b0, 0x49ea50, 0x4a11b0 y 0x4a3850).
[[nodiscard]] Core::Expected<void, std::string> HandleTheFriendsMessage(OldWindowManager& manager, OldWindow& window,
                                                                        const OldWindowGlobals& globals,
                                                                        const OldWindowStepView& view);
// La vtable + 0x1c (0x4a2f70, 0x49dc90, 0x49eb20, 0x4a2010 y 0x4a38a0).
[[nodiscard]] Core::Expected<void, std::string> StepTheFriendsChildren(OldWindowManager& manager, OldWindow& window,
                                                                       std::int32_t arg,
                                                                       const OldWindowGlobals& globals,
                                                                       const OldWindowStepView& view);
// La vtable + 0x58 (0x4a3030, 0x49dcf0, 0x49eb50, 0x4a2070 y 0x4a38e0).
[[nodiscard]] Core::Expected<void, std::string> AfterTheFriendsMove(OldWindowManager& manager, OldWindow& window,
                                                                    const OldWindowGlobals& globals,
                                                                    const OldWindowStepView& view);
// La vtable + 0x30 (0x4a23c0, 0x49ca40, 0x49e690, 0x4a0830 y 0x4a3700).
[[nodiscard]] Core::Expected<void, std::string> SettleTheFriendsKinds(OldWindowManager& manager, OldWindow& window,
                                                                      const OldWindowGlobals& globals,
                                                                      const OldWindowStepView& view);
// El caso 3 de 0x48ee60: el constructor (los ids) y el init (0x4a2280).
[[nodiscard]] Core::Expected<OldWindow, std::string> MakeTheFriendsWindow(OldWindowManager& manager,
                                                                          const OldWindowRequest& request,
                                                                          const OldWindowTexts& texts,
                                                                          const OldWindowMeasure& measure);
// El caso 4: el constructor y el init (0x4a35a0).
[[nodiscard]] OldWindow MakeTheAddFriendBox(OldWindowManager& manager, const OldWindowRequest& request,
                                            const OldWindowTexts& texts);
// 0x49e6e0(id, título) y 0x49e710(id) sobre la pestaña de las ventanas de la de amigos, si está.
void AddTheWindowRow(OldWindowManager& manager, std::int32_t id, std::string_view title);
void ForgetTheWindowRow(OldWindowManager& manager, std::int32_t id);
// 0x4914f0: con la de amigos, su pestaña de las ventanas con las del primer mapa que no son ella ni están
// minimizadas (5), en el orden de los ids.
void FillTheWindowsTab(OldWindowManager& manager);
// 0x490200 con la de amigos: guarda su lugar en +0xc4..+0xe4.
void SaveTheFriendsPlace(OldWindowManager& manager, const OldWindow& window);

// CL9 hito 10: el chat con un amigo y las ventanas de una carta (UI/LetterWindows.cpp).
[[nodiscard]] bool IsALetterKind(OldWindowKind kind);
// Los casos 1, 2, 6 y 7 de 0x48ee60: el constructor y el init (0x493220, 0x4999d0 y 0x497f50). La de leer y la de
// escribir arman el personaje del retrato (OldWindowStepView::portrait); sin él, cortan (P-C212).
[[nodiscard]] Core::Expected<OldWindow, std::string>
MakeTheLetterKind(OldWindowManager& manager, const OldWindowRequest& request, const OldWindowTexts& texts,
                  const OldWindowMeasure& measure, const OldWindowStepView* view);
// La vtable + 0x28 (0x4944c0, 0x49a1f0 y 0x498820).
[[nodiscard]] Core::Expected<void, std::string> HandleTheLetterKindsMessage(OldWindowManager& manager,
                                                                            OldWindow& window,
                                                                            const OldWindowGlobals& globals,
                                                                            const OldWindowStepView& view);
// La vtable + 0x1c (0x495450, 0x49b180 y 0x499780).
[[nodiscard]] Core::Expected<void, std::string> StepTheLetterKindsChildren(OldWindowManager& manager, OldWindow& window,
                                                                           std::int32_t arg,
                                                                           const OldWindowGlobals& globals,
                                                                           const OldWindowStepView& view);
// La vtable + 0x30 (0x493410, 0x499c00 y 0x498350).
[[nodiscard]] Core::Expected<void, std::string> SettleTheLetterKinds(OldWindowManager& manager, OldWindow& window,
                                                                     const OldWindowGlobals& globals,
                                                                     const OldWindowStepView& view);
// El foco del 1 de 0x48ee60 al EDIT del chat (0x48f839) y el de 0x497f50 al del nombre en la de escribir.
void FocusTheChatEdit(OldWindowManager& manager, OldWindow& window, const OldWindowGlobals& globals);
// La vtable + 0x4c: 0x499840 en la de escribir (sin asunto ni cuerpo, sí; si no, la caja 5 con el 1009 y no); las
// demás, 0x48f8f0 (sí).
[[nodiscard]] Core::Expected<bool, std::string> MayCloseTheWindow(OldWindowManager& manager, OldWindow& window,
                                                                  const OldWindowGlobals& globals,
                                                                  const OldWindowStepView& view);
// Los destructores: el del chat (0x493160) sale de la sala (0x493a70); el de leer (0x499900) suelta su carta de
// +0x94 (0x4916b0).
[[nodiscard]] Core::Expected<void, std::string> ForgetTheLetterKind(OldWindowManager& manager, const OldWindow& window);
// 0x4a11b0, la vtable + 0x28 de la pestaña de las cartas.
[[nodiscard]] Core::Expected<void, std::string> HandleTheLettersTabMessage(OldWindowManager& manager, OldWindow& tab,
                                                                           const OldWindowGlobals& globals,
                                                                           const OldWindowStepView& view);
// 0x4a2070, la vtable + 0x58 de la pestaña de las cartas: la marca de todas y los encabezados que ordenan.
void AfterTheLettersTabMove(OldWindowManager& manager, OldWindow& tab, const OldWindowGlobals& globals,
                            const OldWindowStepView& view);
// 0x4a0960 sobre la pestaña: la copia del modelo con la elegida que estaba, y 0x4a09d0(0).
void RefreshTheLetters(OldWindowManager& manager, OldWindow& tab);
// 0x49d0b0 con el 10 y 3: el chat con el amigo (0x4a6430), o el C1 CA si no hay ni se pidió (0x4a6af0, 0x4a6910).
[[nodiscard]] Core::Expected<void, std::string> ChatWithTheFriend(OldWindowManager& manager, std::string_view name,
                                                                  const OldWindowStepView& view);
// 0x49d0b0 con el 10 y 4 (0x49dbd4): la ventana de escribir con el nombre del elegido (0x49cad0), si hay. Da su id
// (0 sin ventana) para la cola 0x49dc37 de la pestaña de amigos.
[[nodiscard]] Core::Expected<std::int32_t, std::string> WriteToTheFriend(OldWindowManager& manager,
                                                                         const FriendEntry* selected,
                                                                         const OldWindowGlobals& globals,
                                                                         const OldWindowStepView& view);
} // namespace Client::UI::Steps
