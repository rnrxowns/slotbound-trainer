#pragma once

// The caller supplies three { operation, value } entries in resource order.
inline constexpr char kResourceLua[] = R"lua(
local function finite(value)
  return type(value) == 'number' and value == value and math.abs(value) < math.huge
end

local function syncJungsu(value)
  local target, ambiguous
  local function inspect(t)
    if finite(rawget(t, 'jungsuBalanceTarget')) and finite(rawget(t, 'jungsuBalance')) then
      if target and not rawequal(target, t) then ambiguous = true end
      target = t
    end
  end

  -- The animation fields may be on a global table or in a game callback's closure.
  inspect(_G)
  for _, v in next, _G do
    if type(v) == 'table' then inspect(v) end
  end

  local debugLib = rawget(_G, 'debug')
  local getupvalue = type(debugLib) == 'table' and rawget(debugLib, 'getupvalue')
  local seen, remaining = { [_G] = true }, 256
  local function visit(v, depth)
    local kind = type(v)
    if ambiguous or depth < 0 or remaining == 0 or seen[v] or (kind ~= 'function' and kind ~= 'table') then
      return
    end
    seen[v], remaining = true, remaining - 1
    if kind == 'table' then
      inspect(v)
      -- Follow named UI subtables and closures, skipping indexed lists of game objects.
      for key, child in next, v do
        if type(key) == 'string' then visit(child, depth - 1) end
        if ambiguous or remaining == 0 then break end
      end
    elseif type(getupvalue) == 'function' then
      local i = 1
      while remaining > 0 and not ambiguous do
        local name, child = getupvalue(v, i)
        if not name then break end
        visit(child, depth - 1)
        i = i + 1
      end
    end
  end
  local love = rawget(_G, 'love')
  if type(love) == 'table' then
    visit(rawget(love, 'update'), 4)
    visit(rawget(love, 'draw'), 4)
    visit(rawget(love, 'mousepressed'), 3)
  end
  visit(rawget(_G, 'ui'), 3)
  if target and not ambiguous and remaining > 0 then
    rawset(target, 'jungsuBalanceTarget', value)
    return true
  end
  return false
end

local fields = { 'coins', 'spinsLeft', 'jungsu' }
local reply = {}
for i, name in ipairs(fields) do
  local value = rawget(_G, name)
  local op, argument = changes[i][1], changes[i][2]
  local status = 'idle'
  if not finite(value) then
    status = 'missing'
  elseif op ~= 'read' then
    local wanted = op == 'add' and (value + argument) or argument
    if not finite(wanted) then
      status = 'invalid'
    elseif op ~= 'lock' or value ~= wanted then
      if value ~= wanted then rawset(_G, name, wanted) end
      status = 'changed'
      if name == 'jungsu' and not syncJungsu(wanted) then status = 'mirror' end
      value = rawget(_G, name)
    end
  end
  reply[#reply + 1] = finite(value) and string.format('%.17g', value) or 'nil'
  reply[#reply + 1] = status
end
return table.concat(reply, '\t')
)lua";
