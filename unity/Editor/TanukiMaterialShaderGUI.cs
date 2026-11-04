using System;
using System.Linq;
using UnityEditor;
using UnityEngine;

namespace Radbot.MapAuthoring.Editor
{
    // Presents only the shader's portable inputs inside Unity's regular Material Inspector.
    public sealed class TanukiMaterialShaderGUI : ShaderGUI
    {
        public override void OnGUI(MaterialEditor editor,MaterialProperty[] properties)
        {
            bool packageSample=editor.targets.OfType<Material>().Any(m=>AssetDatabase.GetAssetPath(m).StartsWith("Packages/",StringComparison.Ordinal));
            if(packageSample)EditorGUILayout.HelpBox("Create an editable copy with Tanuki > Materials, or duplicate this sample into your project.",MessageType.Info);
            using(new EditorGUI.DisabledScope(packageSample))
            {
                var enabled=properties.FirstOrDefault(p=>p.name=="_EmissionEnabled");
                foreach(var property in properties)
                {
                    if((property.propertyFlags&UnityEngine.Rendering.ShaderPropertyFlags.HideInInspector)!=0)continue;
                    if(property.name.StartsWith("_Emission",StringComparison.Ordinal)&&property.name!="_EmissionEnabled"&&enabled!=null&&!enabled.hasMixedValue&&enabled.floatValue<.5f)continue;
                    editor.ShaderProperty(property,property.displayName);
                }
            }
            if(!packageSample)foreach(var material in editor.targets.OfType<Material>())
                material.globalIlluminationFlags=PortableMapMaterials.HasEmission(material)?MaterialGlobalIlluminationFlags.BakedEmissive:MaterialGlobalIlluminationFlags.EmissiveIsBlack;
        }
    }
}
