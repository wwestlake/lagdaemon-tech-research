#include "../Source/ManufacturerReferences.h"
#include "../Source/EngineeringSpecificationChecks.h"
#include <iostream>
int main(int argc,char** argv)
{
    using namespace djehuti::engineering;
    if(argc!=3)return 2;juce::var report;const juce::File file(juce::String::fromUTF8(argv[1]));bool passed=checks(file,report);juce::Array<juce::var> rows;
    const auto check=[&](bool ok,const char* name){auto r=object();put(r,"name",name);put(r,"passed",ok);rows.add(r);passed&=ok;};
    const auto list=manufacturer_references::query(file,{});check((bool)list["ok"]&&list["records"].size()==3,"Three consistent manufacturer identities available");
    for(const auto& summary:*list["records"].getArray())
    {
        const auto query=manufacturer_references::query(file,summary["id"].toString(),true);check((bool)query["ok"]&&(bool)query["assetsVerified"],"Inspect and verify original curve/geometry sources");
        std::map<juce::String,juce::String> params{{"resistance","123"},{"inductance","456"}};juce::String part,error;
        check(manufacturer_references::bind(params,part,query["record"],file.getParentDirectory().getFullPathName(),error),"Bind verified reference to existing part parameters");
        auto serialized=object();for(const auto& p:params)put(serialized,p.first,p.second);
        const auto saved=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("manufacturer-params",".json");saved.replaceWithText(juce::JSON::toString(serialized));
        const auto reopened=juce::JSON::parse(saved);std::map<juce::String,juce::String> restored;for(const auto& p:reopened.getDynamicObject()->getProperties())restored[p.name.toString()]=p.value.toString();saved.deleteFile();
        const auto inspected=manufacturer_references::inspect(restored,true);
        check((bool)inspected["ok"]&&restored==params&&restored["resistance"]=="123"&&restored["inductance"]=="456","Persisted parameter map retains snapshot, identity and original simulator coefficients");
        const auto before=params;part="different manufacturer variant";
        check(!manufacturer_references::bind(params,part,query["record"],file.getParentDirectory().getFullPathName(),error)&&params==before,"Conflicting physical identity rejects binding atomically");
    }
    put(report,"bindings",rows);put(report,"passed",passed);juce::File(juce::String::fromUTF8(argv[2])).replaceWithText(juce::JSON::toString(report));std::cout<<(passed?"PASS":"FAIL")<<" manufacturer engineering and binding checks\n";return passed?0:1;
}
