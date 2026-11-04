using Godot;

[GlobalClass]
public partial class TanukiPreviewPlayer : CharacterBody3D
{
    public enum ViewMode { ThirdPerson, FreeCamera }
    [Export] public ViewMode Mode {get;set;}
    // Unreal OWL 300/500 cm/s with the normal Tanuki .9 role modifier.
    [Export] public float WalkSpeed {get;set;}=2.7f;
    [Export] public float RunSpeed {get;set;}=4.5f;
    [Export] public float JumpSpeed {get;set;}=7;
    [Export] public float Gravity {get;set;}=9.8f;
    [Export] public float Acceleration {get;set;}=20.48f;
    [Export] public float AirControl {get;set;}=.35f;
    [Export] public float Braking {get;set;}=20;
    [Export] public float AirBraking {get;set;}=15;
    [Export] public float GroundFriction {get;set;}=8;
    [Export] public float BrakingFrictionFactor {get;set;}=2;
    [Export] public float GlideSpeed {get;set;}=10;
    [Export] public float GlideFallSpeed {get;set;}=1.5f;
    [Export] public float GlideHoldDelay {get;set;}=.2f;
    [Export] public float GlideMinimumHeight {get;set;}=1;
    public bool IsGliding {get;private set;}
    [Export] public float ClimbSpeed {get;set;}=1.275f;
    [Export] public float MaximumClimbAngle {get;set;}=140;
    public bool IsClimbing {get;private set;}
    Vector3 climbNormal,wallUp,topTarget;
    bool toppingOut;
    [Export] public float FlySpeed {get;set;}=8;
    Camera3D camera;
    MeshInstance3D body;
    float yaw,pitch=.25f,glideHeldTime;
    bool previousJump,previousSwitch;
    public override void _Ready()
    {
        if(Engine.IsEditorHint()){SetPhysicsProcess(false);SetProcessUnhandledInput(false);return;}
        yaw=Rotation.Y+Mathf.Pi;Rotation=Vector3.Zero;CollisionLayer=2;CollisionMask=1;FloorMaxAngle=Mathf.DegToRad(70);
        AddChild(new CollisionShape3D{Shape=new CapsuleShape3D{Radius=.21f,Height=.6f}});
        body=new MeshInstance3D{Mesh=new CapsuleMesh{Radius=.21f,Height=.6f},MaterialOverride=new StandardMaterial3D{AlbedoColor=new Color(.65f,.45f,.24f)}};AddChild(body);
        camera=new Camera3D{Current=true,Near=.03f,TopLevel=true};AddChild(camera);UpdateCamera();Input.MouseMode=Input.MouseModeEnum.Visible;
    }
    public override void _UnhandledInput(InputEvent input)
    {
        if(Engine.IsEditorHint())return;
        if(input is InputEventKey key && key.Pressed && key.Keycode==Key.Escape)Input.MouseMode=Input.MouseModeEnum.Visible;
        // Drag to look without locking or hiding the operating-system cursor.
        if(input is InputEventMouseMotion motion && (motion.ButtonMask&MouseButtonMask.Right)!=0){yaw-=motion.Relative.X*.003f;pitch=Mathf.Clamp(pitch+motion.Relative.Y*.003f,-1.2f,1.2f);}
    }
    public override void _PhysicsProcess(double delta)
    {
        bool jump=Input.IsPhysicalKeyPressed(Key.Space),change=Input.IsPhysicalKeyPressed(Key.F);
        if(change&&!previousSwitch){Mode=Mode==ViewMode.ThirdPerson?ViewMode.FreeCamera:ViewMode.ThirdPerson;Velocity=Vector3.Zero;IsGliding=false;glideHeldTime=0;}
        previousSwitch=change;
        var axis=new Vector2((Input.IsPhysicalKeyPressed(Key.D)?1:0)-(Input.IsPhysicalKeyPressed(Key.A)?1:0),(Input.IsPhysicalKeyPressed(Key.S)?1:0)-(Input.IsPhysicalKeyPressed(Key.W)?1:0));
        if(Mode==ViewMode.ThirdPerson)StepPreview(axis,jump&&!previousJump,Input.IsPhysicalKeyPressed(Key.Shift),(float)delta,jump);
        else
        {
            IsClimbing=false;toppingOut=false;IsGliding=false;glideHeldTime=0;Velocity=Vector3.Zero;
            var direction=camera.GlobalBasis*new Vector3(axis.X,0,axis.Y);direction+=Vector3.Up*((Input.IsPhysicalKeyPressed(Key.E)?1:0)-(Input.IsPhysicalKeyPressed(Key.Q)?1:0));camera.GlobalPosition+=direction.LimitLength()*FlySpeed*(Input.IsPhysicalKeyPressed(Key.Shift)?2:1)*(float)delta;
        }
        previousJump=jump;UpdateCamera();
    }
    public void StepPreview(Vector2 axis,bool jump,bool sprint,float delta,bool glideHeld=false)
    {
        delta=Mathf.Clamp(delta,0,.05f);
        var direction=Basis.FromEuler(new Vector3(0,yaw,0))*new Vector3(axis.X,0,axis.Y);direction=direction.LimitLength();
        if(IsClimbing&&sprint){IsClimbing=false;toppingOut=false;Velocity=Vector3.Zero;}
        if(!IsClimbing&&!sprint&&direction.LengthSquared()>.01f&&Probe(GlobalPosition,direction,.36f,out _,out var entryNormal)&&IsWall(entryNormal))
        {IsClimbing=true;climbNormal=entryNormal;wallUp=Vector3.Up.Slide(climbNormal).Normalized();Velocity=Vector3.Zero;}
        if(IsClimbing)
        {
            IsGliding=false;glideHeldTime=0;Velocity=Vector3.Zero;
            var up=wallUp.Slide(climbNormal).Normalized();
            if(up.LengthSquared()<.01f)up=direction.Slide(climbNormal).Normalized();
            var right=up.Cross(climbNormal).Normalized();
            var displacement=(up*-axis.Y+right*axis.X).LimitLength()*ClimbSpeed*delta;
            var candidate=GlobalPosition+displacement;
            bool supported=Probe(candidate,-climbNormal,.44f,out var contact,out var normal);
            if(!supported&&displacement.LengthSquared()>0)supported=Probe(candidate,-climbNormal-displacement.Normalized(),.51f,out contact,out normal);
            if(toppingOut||(-axis.Y>0&&TryTopOut()))
            {
                var step=topTarget-GlobalPosition;if(step.Y>.02f)step=Vector3.Up*step.Y;
                MoveAndCollide(step.LimitLength(ClimbSpeed*delta));
                if(GlobalPosition.DistanceTo(topTarget)<.04f){IsClimbing=false;toppingOut=false;}
            }
            else if(supported&&IsWall(normal))
            {
                var turn=Quaternion.Identity;
                if(climbNormal.Dot(normal)>-.999f)turn=new Quaternion(climbNormal,normal);
                wallUp=turn*up;climbNormal=normal;
                var correction=contact+normal*.225f-candidate;
                MoveAndCollide(displacement+correction.LimitLength(ClimbSpeed*delta+.03f));
            }
            else IsClimbing=false;
            ConstrainBoundary();return;
        }
        bool grounded=IsOnFloor();glideHeldTime=glideHeld&&!grounded?glideHeldTime+delta:0;
        if(!glideHeld||grounded)IsGliding=false;
        if(!IsGliding && glideHeldTime>=GlideHoldDelay)
        {
            var ray=PhysicsRayQueryParameters3D.Create(GlobalPosition,GlobalPosition+Vector3.Down*(.3f+GlideMinimumHeight),1);ray.Exclude=new Godot.Collections.Array<Rid>{GetRid()};
            if(GetWorld3D().DirectSpaceState.IntersectRay(ray).Count==0)IsGliding=true;
        }
        float speed=IsGliding?GlideSpeed:sprint?RunSpeed:WalkSpeed;
        float rate=direction.LengthSquared()>.001f ? Acceleration*(grounded ? 1 : IsGliding ? .2f : AirControl) : grounded ? Braking : IsGliding ? 5 : AirBraking;
        var horizontal=new Vector3(Velocity.X,0,Velocity.Z);
        horizontal=grounded ? GroundVelocity(horizontal,direction,speed,delta) : horizontal.MoveToward(direction*speed,rate*delta);
        var velocity=new Vector3(horizontal.X,Velocity.Y,horizontal.Z);
        if(grounded){velocity.Y=-.1f;if(jump)velocity.Y=JumpSpeed;}
        if(IsGliding)velocity.Y=Mathf.Lerp(velocity.Y,-GlideFallSpeed,Mathf.Clamp(delta*30,0,1)*Mathf.Clamp(delta*10,0,1));
        else velocity.Y-=Gravity*delta;
        Velocity=velocity;MoveAndSlide();if(IsOnFloor()){IsGliding=false;glideHeldTime=0;}ConstrainBoundary();if(Position.Y<-100){Position=new Vector3(0,1,0);Velocity=Vector3.Zero;IsGliding=false;glideHeldTime=0;}
    }
    bool IsWall(Vector3 normal)
    {float angle=Mathf.RadToDeg(normal.AngleTo(Vector3.Up));return angle>Mathf.RadToDeg(FloorMaxAngle)+2&&angle<=MaximumClimbAngle;}
    bool Ray(Vector3 from,Vector3 to,out Vector3 point,out Vector3 normal)
    {
        var query=PhysicsRayQueryParameters3D.Create(from,to,1);query.Exclude=new Godot.Collections.Array<Rid>{GetRid()};
        var hit=GetWorld3D().DirectSpaceState.IntersectRay(query);point=normal=Vector3.Zero;
        if(hit.Count==0||hit["collider"].AsGodotObject() is not StaticBody3D body)return false;
        Node owner=body;while(owner!=null&&owner is not TanukiMapRoot)owner=owner.GetParent();
        if(owner!=GetParent())return false;
        point=(Vector3)hit["position"];normal=(Vector3)hit["normal"];return true;
    }
    bool Probe(Vector3 origin,Vector3 direction,float distance,out Vector3 point,out Vector3 normal)
    {
        // Sample the capsule's center band; static map geometry alone supports climbing.
        point=normal=Vector3.Zero;float nearest=float.PositiveInfinity;
        foreach(float offset in new[]{0f,.12f,-.12f})
        {
            var start=origin+Vector3.Up*offset;
            if(Ray(start,start+direction.Normalized()*distance,out var p,out var n)&&start.DistanceTo(p)<nearest)
            {nearest=start.DistanceTo(p);point=p;normal=n;}
        }
        return nearest<float.PositiveInfinity;
    }
    bool TryTopOut()
    {
        var start=GlobalPosition+Vector3.Up*.46f-climbNormal*.33f;
        if(!Ray(start,start+Vector3.Down,out var point,out var normal)||normal.AngleTo(Vector3.Up)>FloorMaxAngle)return false;
        var target=point+Vector3.Up*.315f;
        if(target.Y<=GlobalPosition.Y+.02f||target.DistanceTo(GlobalPosition)>1.11f)return false;
        topTarget=target;toppingOut=true;return true;
    }
    Vector3 GroundVelocity(Vector3 velocity,Vector3 input,float speed,float delta)
    {
        // Same steering and braking calculation as Unity's PortablePreviewPlayer.
        bool moving=input.LengthSquared()>.001f;
        float maximum=speed*input.Length();bool over=velocity.Length()>maximum+.0001f;
        if(!moving || over)
        {
            var before=velocity;var reverse=-velocity.Normalized()*Mathf.Max(0,Braking);
            float remaining=delta,friction=Mathf.Max(0,GroundFriction*BrakingFrictionFactor);
            while(remaining>.000001f)
            {
                float step=remaining>1f/33 && friction>0 ? Mathf.Min(1f/33,remaining*.5f) : remaining;
                remaining-=step;velocity+=(-friction*velocity+reverse)*step;
                if(velocity.Dot(before)<=0){velocity=Vector3.Zero;break;}
            }
            if(velocity.LengthSquared()<.01f)velocity=Vector3.Zero;
            if(over && velocity.Length()<maximum && input.Dot(before)>0)velocity=before.Normalized()*maximum;
        }
        else velocity-=(velocity-input.Normalized()*velocity.Length())*Mathf.Clamp(delta*GroundFriction,0,1);
        if(moving)velocity=(velocity+input*Acceleration*delta).LimitLength(Mathf.Max(maximum,velocity.Length()));
        return velocity;
    }
    void ConstrainBoundary()
    {
        if(GetParent() is not TanukiMapRoot root)return;
        foreach(var node in TanukiMapFile.Descendants(root))if(node is TanukiMapBoundary boundary)
        {
            var frame=boundary.GlobalTransform;var p=frame.AffineInverse()*GlobalPosition;var size=frame.Basis.Scale.Abs();
            if(size.X<.01f||size.Y<.01f||size.Z<.01f)return;
            float margin=.21f/Mathf.Min(size.X,size.Z);p.Y=Mathf.Clamp(p.Y,-.5f+.3f/size.Y,.5f-.3f/size.Y);
            if(boundary.Shape==TanukiMapBoundary.BoundaryShape.Box){p.X=Mathf.Clamp(p.X,-.5f+margin,.5f-margin);p.Z=Mathf.Clamp(p.Z,-.5f+margin,.5f-margin);}
            else{var radial=new Vector2(p.X,p.Z).LimitLength(Mathf.Max(0,.5f-margin));p.X=radial.X;p.Z=radial.Y;}
            GlobalPosition=frame*p;return;
        }
    }
    void UpdateCamera()
    {
        camera.GlobalRotation=new Vector3(-pitch,yaw,0);body.Visible=Mode==ViewMode.ThirdPerson;
        if(Mode==ViewMode.ThirdPerson)
        {
            var target=GlobalPosition+Vector3.Up*.2f;var desired=target+camera.GlobalBasis.Z*3.25f+Vector3.Up*.3f;
            var query=PhysicsRayQueryParameters3D.Create(target,desired,1);query.Exclude=new Godot.Collections.Array<Rid>{GetRid()};var hit=GetWorld3D().DirectSpaceState.IntersectRay(query);
            camera.GlobalPosition=hit.Count>0?(Vector3)hit["position"]+((Vector3)hit["normal"])*.1f:desired;camera.LookAt(target);
        }
    }
    public override void _ExitTree(){if(!Engine.IsEditorHint())Input.MouseMode=Input.MouseModeEnum.Visible;}
}
