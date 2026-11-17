#pragma once

#include "overlay.h"

#include <QString>

// Mitgeliefertes Overlay «Chat-Bot» (P14 f2, Plan 7): neutraler Startpunkt für die mitgelieferten
// Aktionen, 1920 × 1080 im Ordner «Chat-Bot». Kennungen von Overlay und Elementen wie in der
// Vorlage (actiontemplate.h, kChatBotOverlayId und kChatBotElements). Nur Qt Core (Regel 9).
//
// - Schlicht: weisse Schrift aus der mitgelieferten Schrift Be Vietnam Pro, keine Rahmen, keine
//   Farbflächen, ein neutrales Platzhalterbild (beim Bau erzeugt, platzhalterbild.cpp). Alle
//   Elemente unsichtbar; die Schritte der Aktionen zeigen sie.
// - Der Kern legt es zusammen mit den mitgelieferten Aktionen an (aktionen.json fehlt), nur wenn es
//   das Overlay noch nicht gibt. Löscht der Nutzer Overlay oder Elemente, zeigen die Schritte
//   «Overlay fehlt» bzw. «Element fehlt», bis er eigene wählt; nichts am Aussehen hängt an ihm.

namespace dss {

class OverlayStore;

// Platzhalterbild in den Ressourcen
inline constexpr char kChatBotPlaceholderResource[] = ":/dss/vorlage/chatbot-platzhalter.png";
inline constexpr char kChatBotOverlayName[] = "Chat-Bot";
inline constexpr char kChatBotFolderName[] = "Chat-Bot";

// Overlay mit allen Elementen; fontFile und imageFile sind Verweise "media/…". Die Adresse setzt
// OverlayStore::addOverlay.
Overlay chatBotOverlay(const QString &fontFile, const QString &imageFile);

enum class ChatBotOverlayResult { Created, Exists, Failed };

// Legt das Overlay an, wenn es fehlt: Schrift und Platzhalterbild in die Mediathek, Overlay, Ordner
// «Chat-Bot» (ein vorhandener Ordner mit dem Namen wird benutzt). Exists: Overlay (oder eine Datei
// mit seiner Kennung) gibt es schon, nichts geändert. warning: angelegt, aber nicht im Ordner (zum
// Beispiel bei ungültiger liste.json).
ChatBotOverlayResult ensureChatBotOverlay(OverlayStore *store, QString *error, QString *warning);

} // namespace dss
