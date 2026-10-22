#pragma once
#include "EngineeringSpecification.h"

namespace manufacturer_references
{
using namespace djehuti::engineering;
inline juce::File bundledLibrary()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("manufacturer-reference-library.json");
}
inline juce::var query(const juce::File& libraryFile,const juce::String& id,bool checkAssets=false)
{
    juce::var library;juce::String error;auto result=object();
    if(!loadLibrary(libraryFile,library,error)){put(result,"ok",false);put(result,"error",error);return result;}
    if(id.isEmpty())
    {
        juce::Array<juce::var> records;
        for(const auto& r:*library["records"].getArray()){auto v=object();for(const auto* k:{"id","name","manufacturer","partNumber"})put(v,k,r[k]);put(v,"variant",specification(r)["variant"]);records.add(v);}
        put(result,"ok",true);put(result,"records",records);return result;
    }
    for(const auto& r:*library["records"].getArray())if(r["id"].toString()==id)
    {
        result=interpret(r);put(result,"record",r);put(result,"assetRoot",libraryFile.getParentDirectory().getFullPathName());
        if(checkAssets){const bool valid=verifyAssets(r,libraryFile.getParentDirectory(),error);put(result,"assetsVerified",valid);if(!valid){put(result,"ok",false);put(result,"error",error);}}
        return result;
    }
    put(result,"ok",false);put(result,"error","No matching manufacturer component ID.");return result;
}
inline bool bind(std::map<juce::String,juce::String>& params,juce::String& manufacturerPart,const juce::var& record,const juce::String& assetRoot,juce::String& error)
{
    if(specification(record).isVoid()||!validate(record,error))return false;
    const auto found=params.find("component_id");
    if((found!=params.end()&&found->second.isNotEmpty()&&found->second!=record["id"].toString())||(manufacturerPart.isNotEmpty()&&manufacturerPart!=record["partNumber"].toString())){error="Existing part identity differs. Use the matching physical component; reference binding cannot substitute a different variant.";return false;}
    params["engineering_record"]=juce::JSON::toString(record,true);
    params["engineering_asset_root"]=assetRoot;params["component_id"]=record["id"].toString();manufacturerPart=record["partNumber"].toString();return true;
}
inline juce::var inspect(const std::map<juce::String,juce::String>& params,bool checkAssets,const juce::String& manufacturerPart={})
{
    const auto found=params.find("engineering_record");auto record=found==params.end()?juce::var():juce::JSON::parse(found->second);auto result=interpret(record);put(result,"record",record);
    const auto identity=params.find("component_id");
    if((bool)result["ok"]&&(identity==params.end()||identity->second!=record["id"].toString()||(manufacturerPart.isNotEmpty()&&manufacturerPart!=record["partNumber"].toString())))
    {put(result,"ok",false);put(result,"error","Part identity has changed since reference binding; review and rebind the correct manufacturer variant.");}
    if(checkAssets&&(bool)result["ok"])
    {
        const auto root=params.find("engineering_asset_root");juce::String error;bool valid=root!=params.end()&&juce::File::isAbsolutePath(root->second)&&verifyAssets(record,juce::File(root->second),error);
        put(result,"assetsVerified",valid);if(!valid){put(result,"ok",false);put(result,"error",error.isEmpty()?"Reference asset root is missing; rebind from a verified portable library.":error);}
    }
    return result;
}
}
