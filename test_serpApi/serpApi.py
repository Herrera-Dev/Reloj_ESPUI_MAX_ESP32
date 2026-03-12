from serpapi import GoogleSearch
import json

params = {
    "q": "Manchester City",
    "location": "Mexico City,Mexico",
    "hl": "en",
    "api_key": "API-KEY",
    "json_restrictor": "sports_results",
    "no_cache": "false"
}

search = GoogleSearch(params)
results = search.get_dict()
sports_results = results.get("sports_results", results)

with open("sports_results.json", "w", encoding="utf-8") as f:
    json.dump(results, f, indent=2, ensure_ascii=False)

def es_fecha_valida(date_str):
    if not date_str: return False
    date_str = date_str.lower()
    
    if "today" in date_str or "tomorrow" in date_str:
        return True
  
    if "yesterday" in date_str:
        return False

    return False
mostrado = False

# game_spotlight
game_spotlight = sports_results.get("game_spotlight")
if game_spotlight:
    fecha_spot = game_spotlight.get("date", "")
    if es_fecha_valida(fecha_spot):
        teams = game_spotlight.get("teams", [])
        status = game_spotlight.get("status")
        
        print("\n- INFORMACIÓN (game_spotlight) -")
        print(f"Estado: {status if status else 'SIN EMPEZAR'}")
        print(f"Partido: {teams[0]['name']} {teams[0].get('score', '')} - {teams[1].get('score', '')} {teams[1]['name']}")
        print(f"Torneo: {game_spotlight.get('league')} - {game_spotlight.get('stage', '')}")
        print(f"Cuándo: {game_spotlight.get('date')} {game_spotlight.get('time', '')}")
        mostrado = True

# games
if not mostrado:
    games = sports_results.get("games", [])
    next_game = None

    for g in games:
        if "status" not in g:  # Si no tiene el campo status, es el siguiente
            next_game = g
            break 

    if next_game:
        teams = next_game.get("teams", [])
        status_game = next_game.get('status', 'SIN EMPEZAR')
        
        print("\n- INFORMACIÓN (Siguiente Partido en Games) -")
        print(f"Estado: {status_game}")
        print(f"Partido: {teams[0]['name']} {teams[0].get('score', '')} - {teams[1].get('score', '')} {teams[1]['name']}")
        print(f"Torneo: {next_game.get('tournament','league')} - {next_game.get('stage', '')}")
        print(f"Cuándo: {next_game.get('date')} {next_game.get('time', '')}")
    else:
        print("\nNo se encontraron próximos partidos.")