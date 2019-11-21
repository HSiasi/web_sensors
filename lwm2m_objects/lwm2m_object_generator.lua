-- --------------------------------------------------------------------
--                   LwM2M Object Generator
-- --------------------------------------------------------------------
local script_version = "1.4"

-- ----------------------------------------------------
-- parseargs: parses XML tag argumments
-- @param s: the arrument string to parse
-- @return arguments in table form
-- ----------------------------------------------------
function parseargs(s)
  local arg = {}
  string.gsub(s, "([%-%w]+)=([\"'])(.-)%2", function (w, _, a)
    arg[w] = a
  end)
  return arg
end

-- ----------------------------------------------------
-- parse: parses an XML document
-- @param s: the XML document to parse
-- @return the XML document in table form
-- ----------------------------------------------------
function parse(s)
  local stack = {}
  local top = {Name=nil,Value=nil,Attributes={},ChildNodes={}}
  table.insert(stack, top)
  local ni,c,label,xarg, empty
  local i, j = 1, 1
  while true do
    ni,j,c,label,xarg, empty = string.find(s, "<(%/?)([%w:]+)(.-)(%/?)>", i)
    if not ni then break end
    local text = string.sub(s, i, ni-1)
    if not string.find(text, "^%s*$") then
      top.Value=(top.Value or "").. text
    end
    if empty == "/" then  -- empty element tag
      table.insert(top.ChildNodes, {Name=label, Value=nil,Attributes=parseargs(xarg), ChildNodes={}})
    elseif c == "" then   -- start tag
      top = {Name=label, Value=nil,Attributes=parseargs(xarg), ChildNodes={}}
      table.insert(stack, top)   -- new level
    else  -- end tag
      local toclose = table.remove(stack)  -- remove top
      top = stack[#stack]
      if #stack < 1 then
        error("nothing to close with "..label)
      end
      if toclose.Name ~= label then
        error("trying to close "..toclose.Name.." with "..label)
      end
      table.insert(top.ChildNodes, toclose)
    end
    i = j+1
  end
  local text = string.sub(s, i)
  if not string.find(text, "^%s*$") then
    stack[#stack].Value=(stack[#stack].Value or "").. text;
  end
  if #stack > 1 then
    error("unclosed "..stack[stack.n].Name)
  end

  return stack[1].ChildNodes[1];
end

-- ----------------------------------------------------
-- dump: dump the contents of an XML document
-- @param xml: the XML document
-- @param fn: not used
-- @param depth: the depth to print at
-- @return None
-- ----------------------------------------------------
function dump(xml, fn, depth)

  if(not xml) then 
    log("nil");
      return;
    end

  if(depth == nil) then depth = 0; end

  local str="";

  for n=0,depth,1 do
    str=str.." ";
  end
      
  print(str.."["..type(xml).."]");
    
  for i,field in pairs(xml) do

    if(type(field)=="table") then
      print("----------------------------------------------")
      print(str.."\t"..tostring(i).." =");
      dump(field, fn, depth+1);
    else 
      if(type(field)=="number") then
        print(str.."\t".."="..field);
      elseif(type(field) == "string") then
        print(str.."\t"..tostring(i).."=".."\""..field.."\"");
      elseif(type(field) == "boolean") then
        print(str.."\t"..tostring(i).."=".."\""..tostring(field).."\"");
      else
        if(not fn)then
          if(type(field)=="function")then
            print(str.."\t"..tostring(i).."()");
          else
            print(str.."\t"..tostring(i).."<userdata=["..type(field).."]>");
          end
        end
      end
    end
  end
end

-- --------------------------------------------------------------------
-- find_tag: finds a tag at the given level
-- @param xml: the XML document
-- @param tag: the tag to find
-- @return the node containing the requested tag or nil
-- --------------------------------------------------------------------
local function find_tag(xml,tag)

 for i,node in pairs(xml.ChildNodes) do
   if(node.Name == tag) then
      return node
   end
 end
end

-- --------------------------------------------------------------------
-- build_resource_name: builds a name for the given resource
-- @param node: the table containing the resource node
-- @return  a string containing the resource name
-- --------------------------------------------------------------------
local function build_resource_name(node)

   local s = ""

   local n = find_tag(node,"Name")
   child = find_tag(node,"Mandatory")

   if (child.Value == "Mandatory") then
      s = string.format("RES_M_%s",string.upper(string.gsub(n.Value, "[^%w%-/ ]","")))
   else
      s = string.format("RES_O_%s",string.upper(string.gsub(n.Value, "[^%w%-/ ]","")))
   end

   return string.gsub(s, "[ /%-]","_")
end

-- --------------------------------------------------------------------
-- has_executable_resources: tests for executable resources
-- @param xml: the XML document
-- @return true if there are executable resources otherwise false
-- --------------------------------------------------------------------
local function has_executable_resources(xml)

   local root = find_tag(xml, "Object")
   if root == nil then return false end

   child = find_tag(root, "Resources")
   if child == nil then return false end

   for k,v in pairs(child.ChildNodes) do
      if(v.Name == "Item") then
         tag = find_tag(v,"Operations")
         if (tag.Value == "E") then return true end
      end
   end
   return false
end

-- --------------------------------------------------------------------
-- create_resource_definitions: creates the resource definitions
-- @param fh: the file handle
-- @param name: the object name
-- @param root: the XML table root node
-- @return none
-- --------------------------------------------------------------------
local function create_resource_definitions(fh, name, root)

   fh:write("-- ----------------------------------------------------\n")
   fh:write("-- Resource IDs for LwM2M ", name, " Object\n")
   fh:write("-- ----------------------------------------------------\n\n")

   fh:write("-- Lua does not have any concept of constants so\n")
   fh:write("-- be careful with these.\n\n")

   local resources = find_tag(find_tag(root, "Object"), "Resources")

   for i,node in pairs(resources.ChildNodes) do
      if(node.Name == "Item") then

        local resId
        local fname = ""

        -- resource id
        for k,v in pairs(node.Attributes) do
           resId = v
        end

        fname = build_resource_name(node)
        fh:write("local ",fname," = ",resId,"\n")
      end
   end

   fh:write("\n")

end

-- --------------------------------------------------------------------
-- Create the resource table
-- --------------------------------------------------------------------
-- --------------------------------------------------------------------
-- create_resource_table: creates the resource table
-- @param fh: the file handle
-- @param fh: the object name
-- @param root: the XML table root node
-- @return none
-- --------------------------------------------------------------------

function create_resource_table(fh, root)

   resources = find_tag(find_tag(root, "Object"), "Resources")

   fh:write("local resource_tbl = {\n")

   for i,node in pairs(resources.ChildNodes) do

      if(node.Name == "Item") then

         local fname = ""
         local operations, instances, ty

         fname = build_resource_name(node)
         fh:write("\n   [",fname,"] = {\n") -- open the resource item table

         for k,v in pairs(node.ChildNodes) do
            -- skip unnecessary meta data
            if (v.Name ~= "Description" and v.Name ~= "RangeEnumeration" and v.Name ~= "Units") then
               fh:write("      ",v.Name," = \"",(v.Value or ""),"\",\n")
            end
         end

         operations = find_tag(node,"Operations")

         if operations.Value ~= "E" then
            -- set the default value
            instances = find_tag(node,"MultipleInstances")
            if (instances.Value == "Multiple") then
               fh:write("      ","Value = {},\n")
            else
               ty = find_tag(node,"Type")
               if (ty.Value == "String" or ty.Value == "Opaque") then
                  fh:write("      ","Value"," = \"","","\",\n")
               elseif (ty.Value == "Integer" or ty.Value == "Float" or ty.Value == "Time" or ty.Value == "Objlnk" or ty.Value == "Boolean") then
                  fh:write("      ","Value = 0,\n")
               else
                  fh:write("      ","Value"," = \"","","\",\n")
               end
            end
         end

         fh:write("   },\n") -- close the resource item table

      end

   end
   fh:write("}\n\n")

end

-- ------------------------------------------------------------
--                      Main
-- -------------------------------------------------------------

if #arg < 1 then
   print ("usage: lua " .. arg[0] .. " <in_file>")
   return
end

-- Open the input file
local file = io.open(arg[1], "r")
if not file then
   print ("Error: could not open file")
   return
end

-- set the default input file
io.input(file)

local buffer = io.read("*all")
io.close(file)

xml = parse(buffer)
if xml == nil then 
   print ("Error: could not parse file")
   return
end

print("Creating object file...")

-- find the object tag
object_node = find_tag(xml, "Object")

-- capture the object information
child = find_tag(object_node, "Name")
--print("Name", child.Value)
name = child.Value

child = find_tag(object_node, "ObjectURN")
--print("ObjectURN", child.Value)
urn = child.Value

child = find_tag(object_node, "LWM2MVersion")
if not child then
   lwm2m_version = "1.0"
else
   lwm2m_version = child.Value
end

child = find_tag(object_node, "ObjectVersion")
if not child then
   object_version = "1.0"
else
   object_version = child.Value
end

child = find_tag(object_node, "ObjectID")
--print("ObjectID", child.Value)
objectID = child.Value

child = find_tag(object_node, "MultipleInstances")
--print("MultipleInstances", child.Value)
multiple_instances = child.Value

child = find_tag(object_node, "Mandatory")
--print("Mandatory", child.Value)
mandatory = child.Value

local formatted_name = string.format("object_%s", string.lower(string.gsub(name, " ","_")))

-- Opens a file in update mode
file = io.open(string.format("%s.lua",formatted_name), "w+")

-- --------------------------------------------------------------------
-- Build the file header
-- --------------------------------------------------------------------

-- Title
file:write("-- ----------------------------------------------------\n")
file:write("-- ", name, " Object\n");
file:write("-- Generated by LwM2M Object Generator version ", script_version, "\n");
file:write("-- ----------------------------------------------------\n")
file:write("\n")

-- Mandatory includes
file:write("require (\"lwm2m_object_table\")\nrequire (\"lwm2m_defs\")\nrequire (\"utils\")\n\n")

create_resource_definitions(file, name, xml)

file:write("-- ----------------------------------------------------\n")
file:write("-- Globals\n")
file:write("-- ----------------------------------------------------\n")
file:write(formatted_name," = {}\n")
file:write(formatted_name,".objectId = ",objectID,"\n")
file:write(formatted_name,".name = \"",formatted_name,"\"\n")
file:write("\n")
file:write("-- Add this object to the global object table\n")
file:write("lwm2m_object_tbl_add(",formatted_name,".objectId, ", formatted_name, ".name)\n")
file:write("\n")

-- --------------------------------------------------------------------
-- Build the object table
-- --------------------------------------------------------------------

file:write("local object_table = {\n\n")
file:write("   ","Name = \"", name,"\",\n")
file:write("   ","ObjectId = \"",objectID,"\",\n")
file:write("   ","LwM2MVersion = \"",lwm2m_version,"\",\n")
file:write("   ","ObjectVersion = \"",object_version, "\",\n")
file:write("   ","MultipleInstances = \"",multiple_instances,"\",\n")
file:write("   ","Mandatory = \"", mandatory,"\",\n")
file:write("\n")
file:write("   instance = {}\n")
file:write("}\n\n")

-- --------------------------------------------------------------------
-- Build the resource table
-- --------------------------------------------------------------------

create_resource_table(file, xml)

-- --------------------------------------------------------------------
-- Build the functions
-- --------------------------------------------------------------------

file:write("-- ----------------------------------------------------\n")
file:write("-- Standard Functions\n")
file:write("-- ----------------------------------------------------\n")

-- --------------------------------------------------------------------
--                            Load
-- --------------------------------------------------------------------

file:write("-- ----------------------------------------------------\n")
file:write("-- Load: Loads an object into the object table\n")
file:write("-- @param t: the object to be loaded\n")
file:write("-- @return  None\n")
file:write("-- ----------------------------------------------------\n")
file:write("function ",formatted_name,".load(t)\n")
file:write("   object_table = t\n")
file:write("end\n")
file:write("\n")

-- --------------------------------------------------------------------
--                      get_resource_table
-- --------------------------------------------------------------------

file:write("-- ----------------------------------------------------\n")
file:write("-- Get Resource Table: Returns the resource table\n")
file:write("-- @return  The resource table\n")
file:write("-- ----------------------------------------------------\n")
file:write("function ",formatted_name,".get_resource_table()\n")
file:write("   return resource_tbl\n")
file:write("end\n")
file:write("\n")

-- --------------------------------------------------------------------
--                      get_resource_type
-- --------------------------------------------------------------------

file:write("-- ----------------------------------------------------\n")
file:write("-- Get Resource Type: Returns the resource type\n")
file:write("-- @param res: resource identifier.\n")
file:write("-- @return  The LwM2M resource type as a string\n")
file:write("-- ----------------------------------------------------\n")
file:write("function ",formatted_name,".get_resource_type(res)\n")
file:write("   return resource_tbl[res].Type\n")
file:write("end\n")
file:write("\n")

-- --------------------------------------------------------------------
--                      get_object_table
-- --------------------------------------------------------------------

file:write("-- ----------------------------------------------------\n")
file:write("-- Get Object Table: Returns the object table\n")
file:write("-- @return  The object table\n")
file:write("-- ----------------------------------------------------\n")
file:write("function ",formatted_name,".get_object_table()\n")
file:write("   return object_table\n")
file:write("end\n")
file:write("\n")

-- --------------------------------------------------------------------
--                         Delete
-- --------------------------------------------------------------------

file:write("-- ----------------------------------------------------\n")
file:write("-- Delete: Delete an Object Instance\n")
file:write("-- @param inst: object instance identifier.\n")
file:write("-- @return  COAP response code\n")
file:write("-- ----------------------------------------------------\n")
file:write("function ",formatted_name,".delete (inst)\n")
file:write("\n")
file:write("   if  object_table.instance[inst] == nil then\n")
file:write("      return coap.COAP_404_NOT_FOUND\n")
file:write("   end\n")
file:write("\n")
file:write("   -- delete the instance from memory\n")
file:write("   object_table.instance[inst] = nil\n")
file:write("\n")
file:write("   return coap.COAP_202_DELETED\n")
file:write("\n")
file:write("end\n")
file:write("\n")

-- --------------------------------------------------------------------
--                         Write
-- --------------------------------------------------------------------

file:write("-- ----------------------------------------------------\n")
file:write("-- Write: Write a value to a resource\n")
file:write("-- @param inst:    object instance identifier.\n")
file:write("-- @param res:     the resource identifier\n")
file:write("-- @param iface:   indicates the interface the operation\n")
file:write("--                 was originated on\n")
file:write("-- @param replace: true if the operation should replace\n")
file:write("--                 previous resource.\n")
file:write("-- @param value:   the value to be written\n")
file:write("-- @return  COAP result code\n")
file:write("-- ----------------------------------------------------\n")
file:write("function ",formatted_name,".write (inst, res, iface, replace, value, userdata)\n")
file:write("\n")
file:write("   if  object_table.instance[inst] == nil or object_table.instance[inst].resource[res] == nil then\n")
file:write("      return coap.COAP_404_NOT_FOUND\n")
file:write("   end\n")
file:write("\n")

file:write("   if iface == lwm2m_interface_type.LWM2M_DM_INTERFACE then\n")
file:write("      -- This an operation on the Device Management interface\n")
file:write("      if string.find(object_table.instance[inst].resource[res].Operations, \"W\") == nil then\n")
file:write("         -- The target resource does not support the Write operation\n")
file:write("         return coap.COAP_405_METHOD_NOT_ALLOWED, nil, nil\n")
file:write("      end\n")
file:write("   else\n")
file:write("      if string.find(object_table.instance[inst].resource[res].Operations, \"W\") == nil and\n")
file:write("         string.find(object_table.instance[inst].resource[res].Operations, \"\") == nil then\n")
file:write("         return coap.COAP_405_METHOD_NOT_ALLOWED\n")
file:write("      end\n")
file:write("   end\n")
file:write("\n")

file:write("   local t = type(value)\n")
file:write("\n")
file:write("   if t == \"table\" then\n")
file:write("\n")
file:write("      if replace == true then\n")    
file:write("        object_table.instance[inst].resource[res].Value = {}\n")
file:write("      end\n")
file:write("\n")
file:write("      -- this is a multi-instance resource; iterate the table and overwrite the values\n")
file:write("      -- if the resource instance exists otherwise create a new resource instance\n")
file:write("      -- and set the value\n")
file:write("\n")
file:write("      for ri, val in pairs(value) do\n")
file:write("         object_table.instance[inst].resource[res].Value[ri] = val\n")
file:write("      end\n")
file:write("\n")
file:write("   else\n")
file:write("      object_table.instance[inst].resource[res].Value = value\n")
file:write("   end\n")
file:write("\n")
file:write("   return coap.COAP_204_CHANGED\n")
file:write("\n")
file:write("end\n")
file:write("\n")

-- --------------------------------------------------------------------
--                         Read
-- --------------------------------------------------------------------

file:write("-------------------------------------------------------\n")
file:write("-- Read: Access the value of a resource\n")
file:write("-- @param inst: object instance identifier.\n")
file:write("-- @param res:  resource identifier.\n")
file:write("-- @param dm:   true if the operation is on the DM\n")
file:write("--              interface.\n")
file:write("-- @return  COAP result code, resource type, value\n")
file:write("-- \n")
file:write("-- @comments: The value parameter may be in the form of\n")
file:write("--            a Lua Table.\n")
file:write("-------------------------------------------------------\n")
file:write("function ",formatted_name,".read (inst, res, dm)\n")
file:write("\n")
file:write("   if object_table.instance[inst] == nil or object_table.instance[inst].resource[res] == nil then\n")
file:write("      return coap.COAP_404_NOT_FOUND, nil, nil\n")
file:write("   end\n")
file:write("\n")

file:write("   if dm == true then\n")
file:write("      -- This an operation on the Device Management interface\n")
file:write("      if string.find(object_table.instance[inst].resource[res].Operations, \"R\") == nil then\n")
file:write("         -- The target resource does not support the Read operation\n")
file:write("         return coap.COAP_405_METHOD_NOT_ALLOWED, nil, nil\n")
file:write("      end\n")
file:write("   else\n")
file:write("      if string.find(object_table.instance[inst].resource[res].Operations, \"R\") == nil and\n") 
file:write("         string.find(object_table.instance[inst].resource[res].Operations, \"\") == nil then\n")
file:write("         return coap.COAP_405_METHOD_NOT_ALLOWED\n")
file:write("      end\n")
file:write("   end\n")
file:write("\n")

file:write("   value = object_table.instance[inst].resource[res].Value\n")
file:write("   vtype = object_table.instance[inst].resource[res].Type\n")
file:write("\n")
file:write("   return coap.COAP_205_CONTENT, vtype, value\n")
file:write("\n")
file:write("end\n")
file:write("\n")

-- --------------------------------------------------------------------
--                         Execute
-- --------------------------------------------------------------------

if (has_executable_resources(xml) == true) then

   file:write("-------------------------------------------------------\n")
   file:write("-- Execute: Initiate some action on a resource\n")
   file:write("-- @param inst:      object instance identifier.\n")
   file:write("-- @param res        resource identifier.\n")
   file:write("-- @param buffer:    Null terminated string containing\n")
   file:write("--                   optional arguments associated with the\n")
   file:write("--                   command\n")
   file:write("-- @param length:    The length of buffer in bytes\n")
   file:write("-- @param userdata:  Caller specific user data\n")
   file:write("-- @return  COAP response code\n")
   file:write("-------------------------------------------------------\n")
   file:write("function ",formatted_name,".execute (inst, res, buffer, length, userdata)\n")
   file:write("\n")
   file:write("   local result = coap.COAP_400_BAD_REQUEST\n")
   file:write("\n")
   file:write("   if  object_table.instance[inst] == nil then\n")
   file:write("      return coap.COAP_404_NOT_FOUND\n")
   file:write("   end\n")
   file:write("\n")

   -- find the executable resources and add a stub conditional
   local resources = find_tag(object_node, "Resources")
   local count = 0

   -- Insert executable resource Ids
   for i, node in pairs(resources.ChildNodes) do
      if(node.Name == "Item") then
         local x = find_tag(node,"Operations")
         if (x.Value == "E") then
            local name = build_resource_name(node)
            if count == 0 then
               file:write("   if res == ",name," then\n")
               file:write("      print (\"",name,"\", buffer, length)\n")
               file:write("      result = coap.COAP_204_CHANGED\n")
            else
               file:write("   elseif res == ",name," then\n")
               file:write("      print (\"",name,"\", buffer, length)\n")
               file:write("      result = coap.COAP_204_CHANGED\n")
            end
            count = count + 1
         end
      end
   end

   if count > 0 then
      file:write("   else\n")
      file:write("      result = coap.COAP_404_NOT_FOUND\n")
      file:write("   end\n")
   end

   file:write("   return result\n")
   file:write("end\n")
   file:write("\n")
end

-- --------------------------------------------------------------------
--                         Discover
-- --------------------------------------------------------------------

file:write("-------------------------------------------------------\n")
file:write("-- Discover: Discover LwM2M Attributes\n")
file:write("-- @param inst: object instance identifier.\n")
file:write("-- @param res:  resource identifier.\n")
file:write("-- @return  COAP response code\n")
file:write("-------------------------------------------------------\n")
file:write("function ",formatted_name,".discover (inst, res)\n")
file:write("\n")
file:write("   if object_table.instance[inst] == nil or object_table.instance[inst].resource[res] == nil then\n")
file:write("      return coap.COAP_404_NOT_FOUND\n")
file:write("   end\n")
file:write("\n")
file:write("   return coap.COAP_205_CONTENT\n")
file:write("end\n")
file:write("\n")

-- --------------------------------------------------------------------
--                         Create
-- --------------------------------------------------------------------

file:write("-- ----------------------------------------------------\n")
file:write("-- Create: Create an object instance\n")
file:write("-- @param inst: object instance identifier.\n")
file:write("-- @return COAP response code\n")
file:write("-------------------------------------------------------\n")
file:write("function ",formatted_name,".create (inst)\n")
file:write("\n")

if (multiple_instances == "Multiple") then
   file:write("   -- make sure the instance does not already exist\n")
   file:write("   if object_table.instance[inst] ~= nil then\n")
   file:write("      return coap.COAP_400_BAD_REQUEST\n")
   file:write("   end\n")
   file:write("\n")

   file:write("   -- initialize an object instance\n")
   file:write("   object_table.instance[inst] = {\n")
else
   file:write("   -- this is a single instance object\n")
   file:write("   if inst ~= 0 or object_table.instance[inst] ~= nil then\n")
   file:write("      return coap.COAP_400_BAD_REQUEST\n")
   file:write("   end\n")
   file:write("\n")

   file:write("   -- initialize an object instance\n")
   file:write("   object_table.instance[0] = {\n")
end

file:write("\n")
file:write("      resource = utils_copy_table(resource_tbl)\n")
file:write("   }\n")
file:write("\n")
file:write("   return coap.COAP_201_CREATED\n")
file:write("\n")
file:write("end\n")
file:write("\n")

file:write("-- return the object\n")
file:write("return ",formatted_name,"\n")
file:write("\n")

-- close the file
file:close()

print("Done")


