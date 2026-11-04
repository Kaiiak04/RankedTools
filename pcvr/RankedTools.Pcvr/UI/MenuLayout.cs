using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace RankedTools.Pcvr.UI;

internal static class MenuLayout
{
    public static void Configure(GameObject root)
    {
        // BSML's scroll content normally fits its preferred width. Keep our
        // content tied to the viewport so long messages cannot widen it.
        foreach(var fitter in root.GetComponentsInChildren<ContentSizeFitter>(true))
        {
            if(fitter.gameObject.name!="BSMLScrollViewContent")continue;
            fitter.horizontalFit=ContentSizeFitter.FitMode.Unconstrained;
            var rect=(RectTransform)fitter.transform;
            rect.anchorMin=new Vector2(0,rect.anchorMin.y);
            rect.anchorMax=new Vector2(1,rect.anchorMax.y);
            rect.sizeDelta=new Vector2(0,rect.sizeDelta.y);
        }
        foreach(var text in root.GetComponentsInChildren<TextMeshProUGUI>(true))
        {
            if(text.gameObject.name!="BSMLText")continue;
            // TextTag has no LayoutElement. Override its inferred minimum
            // width, but preserve TMP's calculated multiline height.
            var element=text.GetComponent<LayoutElement>()??text.gameObject.AddComponent<LayoutElement>();
            element.minWidth=0;
            element.preferredWidth=0;
            element.flexibleWidth=1;
        }
    }
}
