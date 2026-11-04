using Godot;
using System.Linq;

public static class TanukiPreviewSetup
{
    public static void Ensure(TanukiMapRoot root)
    {
        if(!root.GetChildren().OfType<TanukiPreviewPlayer>().Any())
        {
            var start=TanukiMapFile.Descendants(root).OfType<TanukiPlayerStart>().FirstOrDefault();
            var player=new TanukiPreviewPlayer{Name="Player Character",Position=start?.Position??new Vector3(0,1,0),Rotation=start?.Rotation??Vector3.Zero};TanukiMapFile.Own(root,player);
        }
        TanukiMapEnvironment.Apply(root);
        TanukiMapEnvironment.References(root);
    }
}
