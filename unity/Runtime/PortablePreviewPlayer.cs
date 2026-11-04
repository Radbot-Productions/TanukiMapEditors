using System.Collections.Generic;
using UnityEngine;

namespace Radbot.MapAuthoring
{
    /// <summary>Unity-only authoring navigation proxy. Never part of the portable scene.</summary>
    [DisallowMultipleComponent,RequireComponent(typeof(CharacterController))]
    [AddComponentMenu("Tanuki/Player Character")]
    public sealed class PortablePreviewPlayer : MonoBehaviour
    {
        public enum CameraMode { ThirdPerson, FreeCamera }
        public CameraMode mode;
        public PortableMapRoot map;
        // BP_TanukiCharacter_FirstPass CollisionCylinder: radius 21 cm, half-height 30 cm.
        [Min(.01f)] public float radius=.21f;
        [Min(.02f)] public float height=.60f;
        // OWL ground 300/500 cm/s, normal Tanuki role multiplier .9; GAS jump 700 cm/s.
        [Min(.1f)] public float walkSpeed=2.7f,runSpeed=4.5f,climbSpeed=1.275f,flySpeed=8f;
        public float jumpHeight=2.5f,gravity=9.8f,lookSensitivity=2f,cameraDistance=3.25f;
        public float acceleration=20.48f,airControl=.35f,braking=20f,airBraking=15f;
        public float groundFriction=8f,brakingFrictionFactor=2f;
        public float glideSpeed=10f,glideFallSpeed=1.5f,glideHoldDelay=.2f,glideMinimumHeight=1f;
        [SerializeField,HideInInspector] int movementProfileVersion;
        public bool objectFormClimbing=true;
        [Range(90,180)] public float maximumClimbAngle=140f;
        public LayerMask collisionMask=~0;
        public bool IsClimbing {get;private set;}
        public bool IsGliding {get;private set;}
        public Vector3 ClimbNormal => normal;
        CharacterController body;
        Camera view;
        GameObject visual;
        Material material;
        readonly List<Camera> previousCameras=new List<Camera>();
        readonly List<AudioListener> previousListeners=new List<AudioListener>();
        CameraMode previousMode;
        CursorLockMode previousLock;
        bool previousCursor,ownsCursor;
        float yaw,pitch=15,verticalSpeed;
        float glideHeldTime;
        Vector3 horizontalVelocity;
        Vector3 normal,wallUp=Vector3.up,topTarget;
        bool toppingOut;

        public static bool IsPreview(Transform target) => target && (target.GetComponentInParent<PortablePreviewPlayer>() || target.GetComponentInParent<PortableMapPreviewReference>());
        public void InitializePreview()
        {
            // Upgrade the original preview defaults without replacing custom tuning.
            if(movementProfileVersion==0)
            {
                if(Mathf.Approximately(walkSpeed,3.5f))walkSpeed=2.7f;
                if(Mathf.Approximately(runSpeed,5.5f))runSpeed=4.5f;
                if(Mathf.Approximately(climbSpeed,1.7f))climbSpeed=1.275f;
                if(Mathf.Approximately(jumpHeight,.6f))jumpHeight=2.5f;
                if(Mathf.Approximately(gravity,20))gravity=9.8f;
                if(Mathf.Approximately(cameraDistance,2.6f))cameraDistance=3.25f;
                movementProfileVersion=1;
            }
            body=GetComponent<CharacterController>();
            height=Mathf.Max(height,radius*2);body.height=height;body.radius=radius;body.center=Vector3.zero;
            body.skinWidth=.01f;body.stepOffset=Mathf.Min(.2f,height*.4f);body.slopeLimit=70;
            // At high FPS, the first acceleration step can be below Unity's
            // default 1 mm cutoff. Discarding it also resets our measured velocity,
            // trapping the player at rest until a sufficiently long frame occurs.
            body.minMoveDistance=0;
            previousMode=mode;yaw=transform.eulerAngles.y;
            if(!view)
            {
                var go=new GameObject("Preview Camera");go.transform.SetParent(transform,false);view=go.AddComponent<Camera>();view.nearClipPlane=.03f;
                go.AddComponent<AudioListener>();
                visual=GameObject.CreatePrimitive(PrimitiveType.Capsule);visual.name="Preview Body";visual.transform.SetParent(transform,false);
                var collider=visual.GetComponent<Collider>();collider.enabled=false;if(Application.isPlaying)Destroy(collider);else DestroyImmediate(collider);
                visual.transform.localScale=new Vector3(radius*2,height*.5f,radius*2);
                var shader=Shader.Find(UnityEngine.Rendering.GraphicsSettings.currentRenderPipeline?"Universal Render Pipeline/Lit":"Standard");
                if(shader) {material=new Material(shader);material.color=new Color(.65f,.45f,.24f);visual.GetComponent<Renderer>().sharedMaterial=material;}
                UpdateCamera(0);if(mode==CameraMode.FreeCamera) view.transform.position=transform.position-transform.forward*cameraDistance+Vector3.up;
            }
        }
        void Awake() {if(Application.isPlaying) InitializePreview();}
        void OnEnable()
        {
            if(!Application.isPlaying) return;
            InitializePreview();
            view.enabled=true;view.GetComponent<AudioListener>().enabled=true;
            foreach(var root in gameObject.scene.GetRootGameObjects())
            {
                foreach(var c in root.GetComponentsInChildren<Camera>()) if(c!=view && c.enabled) {previousCameras.Add(c);c.enabled=false;}
                foreach(var a in root.GetComponentsInChildren<AudioListener>()) if(a.gameObject!=view.gameObject && a.enabled) {previousListeners.Add(a);a.enabled=false;}
            }
        }
        void Update()
        {
            if(!Application.isPlaying || !view) return;
#if ENABLE_LEGACY_INPUT_MANAGER
            if(Input.GetKeyDown(KeyCode.Escape)) ReleaseCursor();
            if(Input.GetMouseButtonDown(1) && Application.isFocused) CaptureCursor();
            if(!Application.isFocused) return;
            if(Input.GetKeyDown(KeyCode.F)) SetCameraMode(mode==CameraMode.ThirdPerson?CameraMode.FreeCamera:CameraMode.ThirdPerson);
            Vector2 look=Cursor.lockState==CursorLockMode.Locked?new Vector2(Input.GetAxisRaw("Mouse X"),Input.GetAxisRaw("Mouse Y")):Vector2.zero;
            float lift=(Input.GetKey(KeyCode.E)?1:0)-(Input.GetKey(KeyCode.Q)?1:0);
            Simulate(new Vector2(Input.GetAxisRaw("Horizontal"),Input.GetAxisRaw("Vertical")),look,Input.GetKeyDown(KeyCode.Space),Input.GetKey(KeyCode.LeftShift),lift,Time.deltaTime,Input.GetKey(KeyCode.Space));
#endif
        }
        public void SetCameraMode(CameraMode value)
        {
            mode=value;previousMode=value;
            IsGliding=false;glideHeldTime=0;horizontalVelocity=Vector3.zero;verticalSpeed=0;
            if(visual) visual.SetActive(mode==CameraMode.ThirdPerson);
        }
        public void Simulate(Vector2 movement,Vector2 look,bool jump,bool sprint,float lift,float deltaTime,bool glideHeld=false)
        {
            if(!body) body=GetComponent<CharacterController>();
            if(mode!=previousMode) SetCameraMode(mode);
            float dt=Mathf.Clamp(deltaTime,0,.05f);yaw+=look.x*lookSensitivity;pitch=Mathf.Clamp(pitch-look.y*lookSensitivity,-75,80);
            movement=Vector2.ClampMagnitude(movement,1);
            var rotation=Quaternion.Euler(pitch,yaw,0);
            if(mode==CameraMode.FreeCamera)
            {if(view) {view.transform.rotation=rotation;view.transform.position+=(rotation*new Vector3(movement.x,0,movement.y)+Vector3.up*lift)*(flySpeed*(sprint?3:1)*dt);}return;}
            var heading=Quaternion.Euler(0,yaw,0);
            Vector3 direction=heading*new Vector3(movement.x,0,movement.y);
            if(IsClimbing && sprint) {IsClimbing=false;toppingOut=false;verticalSpeed=0;}
            if(!IsClimbing && objectFormClimbing && !sprint && direction.sqrMagnitude>.01f &&
                Probe(transform.position,direction,radius+.15f,out var entry) && IsWall(entry.normal))
            {IsClimbing=true;normal=entry.normal;wallUp=Vector3.ProjectOnPlane(Vector3.up,normal).normalized;verticalSpeed=0;}
            if(IsClimbing)
            {
                IsGliding=false;glideHeldTime=0;horizontalVelocity=Vector3.zero;
                Vector3 up=Vector3.ProjectOnPlane(wallUp,normal).normalized;
                if(up.sqrMagnitude<.01f) up=Vector3.ProjectOnPlane(heading*Vector3.forward,normal).normalized;
                Vector3 right=Vector3.Cross(normal,up).normalized;
                Vector3 displacement=(up*movement.y+right*movement.x)*(climbSpeed*dt);
                Vector3 candidate=transform.position+displacement;
                RaycastHit contact;
                bool supported=Probe(candidate,-normal,radius+.23f,out contact);
                if(!supported && displacement.sqrMagnitude>0)
                    supported=Probe(candidate,-normal-displacement.normalized,radius+.3f,out contact);
                if(toppingOut || (movement.y>0 && TryTopOut(Vector3.up))) AdvanceTopOut(dt);
                else if(supported && IsWall(contact.normal))
                {
                    wallUp=Quaternion.FromToRotation(normal,contact.normal)*up;normal=contact.normal;
                    Vector3 correction=contact.point+normal*(radius+.015f)-candidate;
                    body.Move(displacement+Vector3.ClampMagnitude(correction,climbSpeed*dt+.03f));
                    var face=Vector3.ProjectOnPlane(-normal,Vector3.up);
                    if(face.sqrMagnitude>.01f) transform.rotation=Quaternion.LookRotation(face,Vector3.up);
                }
                else {IsClimbing=false;verticalSpeed=0;}
                // Object form cannot jump on the wall, including while idle.
            }
            if(!IsClimbing)
            {
                bool grounded=body.isGrounded;
                glideHeldTime=glideHeld&&!grounded?glideHeldTime+dt:0;
                if(!glideHeld||grounded)IsGliding=false;
                if(!IsGliding && glideHeldTime>=glideHoldDelay &&
                    !Physics.Raycast(transform.position,Vector3.down,height*.5f+glideMinimumHeight,collisionMask,QueryTriggerInteraction.Ignore))IsGliding=true;
                if(grounded && verticalSpeed<0)verticalSpeed=-1;
                if(jump && grounded)verticalSpeed=Mathf.Sqrt(2*gravity*jumpHeight);
                if(IsGliding)verticalSpeed=Mathf.Lerp(verticalSpeed,-glideFallSpeed,Mathf.Clamp01(dt*30)*Mathf.Clamp01(dt*10));
                else verticalSpeed-=gravity*dt;
                float speed=IsGliding?glideSpeed:sprint?runSpeed:walkSpeed;
                float rate=direction.sqrMagnitude>.001f ? acceleration*(grounded ? 1 : IsGliding ? .2f : airControl) : grounded ? braking : IsGliding ? 5f : airBraking;
                horizontalVelocity=grounded ? GroundVelocity(horizontalVelocity,direction,speed,dt) : Vector3.MoveTowards(horizontalVelocity,direction*speed,rate*dt);
                var before=transform.position;
                body.Move((horizontalVelocity+Vector3.up*verticalSpeed)*dt);
                if((body.collisionFlags&CollisionFlags.Above)!=0 && verticalSpeed>0)verticalSpeed=0;
                if(dt>0)horizontalVelocity=Vector3.ProjectOnPlane((transform.position-before)/dt,Vector3.up);
                if(body.isGrounded){IsGliding=false;glideHeldTime=0;}
                if(direction.sqrMagnitude>.01f) transform.rotation=Quaternion.LookRotation(direction,Vector3.up);
            }
            ConstrainBoundary();UpdateCamera(dt);
        }
        Vector3 GroundVelocity(Vector3 velocity,Vector3 input,float speed,float dt)
        {
            // Mirror CharacterMovement::CalcVelocity: steer with ground friction,
            // and apply both friction and constant deceleration when braking.
            bool moving=input.sqrMagnitude>.001f;
            float maximum=speed*input.magnitude;
            bool over=velocity.magnitude>maximum+.0001f;
            if(!moving || over)
            {
                var before=velocity;
                var reverse=-velocity.normalized*Mathf.Max(0,braking);
                float remaining=dt,friction=Mathf.Max(0,groundFriction*brakingFrictionFactor);
                while(remaining>.000001f)
                {
                    float step=remaining>1f/33 && friction>0 ? Mathf.Min(1f/33,remaining*.5f) : remaining;
                    remaining-=step;velocity+=(-friction*velocity+reverse)*step;
                    if(Vector3.Dot(velocity,before)<=0){velocity=Vector3.zero;break;}
                }
                if(velocity.sqrMagnitude<.01f)velocity=Vector3.zero;
                if(over && velocity.magnitude<maximum && Vector3.Dot(input,before)>0)velocity=before.normalized*maximum;
            }
            else velocity-=(velocity-input.normalized*velocity.magnitude)*Mathf.Clamp01(dt*groundFriction);
            if(moving)velocity=Vector3.ClampMagnitude(velocity+input*acceleration*dt,Mathf.Max(maximum,velocity.magnitude));
            return velocity;
        }
        bool IsWall(Vector3 n) {float angle=Vector3.Angle(n,Vector3.up);return angle>body.slopeLimit+2 && angle<=maximumClimbAngle;}
        bool Probe(Vector3 origin,Vector3 direction,float distance,out RaycastHit result)
        {
            result=default;float nearest=float.PositiveInfinity;
            foreach(var hit in Physics.SphereCastAll(origin,radius*.65f,direction.normalized,distance,collisionMask,QueryTriggerInteraction.Ignore))
            {
                if(IsPreview(hit.transform) || hit.collider.GetComponentInParent<PortableMapBoundary>() ||
                    (map && hit.collider.GetComponentInParent<PortableMapRoot>()!=map) || hit.distance>=nearest) continue;
                if(hit.rigidbody && !hit.rigidbody.isKinematic) continue;
                nearest=hit.distance;result=hit;
            }
            return nearest<float.PositiveInfinity;
        }
        bool TryTopOut(Vector3 up)
        {
            Vector3 start=transform.position+up*(height*.6f+.1f)-normal*(radius+.12f);
            if(!Physics.Raycast(start,Vector3.down,out var hit,height+.4f,collisionMask,QueryTriggerInteraction.Ignore) ||
                IsPreview(hit.transform) || (map && hit.collider.GetComponentInParent<PortableMapRoot>()!=map) || Vector3.Angle(hit.normal,Vector3.up)>body.slopeLimit) return false;
            Vector3 target=hit.point+Vector3.up*(height*.5f+.015f);
            if(target.y<=transform.position.y+.02f) return false;
            Vector3 delta=target-transform.position;
            if(delta.magnitude>height+radius+.3f) return false;
            topTarget=target;toppingOut=true;return true;
        }
        void AdvanceTopOut(float dt)
        {
            Vector3 delta=topTarget-transform.position;
            // Rise above the lip before moving over it; both stages use the capsule sweep.
            if(delta.y>.02f) delta=Vector3.up*delta.y;
            body.Move(Vector3.ClampMagnitude(delta,climbSpeed*dt));
            if(Vector3.Distance(transform.position,topTarget)<.04f) {IsClimbing=false;toppingOut=false;verticalSpeed=0;}
        }
        void ConstrainBoundary()
        {
            if(!map) return;
            var bounds=map.GetComponentInChildren<PortableMapBoundary>();if(!bounds) return;
            Vector3 p=bounds.transform.InverseTransformPoint(transform.position),s=bounds.transform.lossyScale;
            Vector3 localUp=bounds.transform.InverseTransformVector(Vector3.up)*(height*.5f-radius);
            Vector3 margin=new Vector3(radius/Mathf.Abs(s.x)+Mathf.Abs(localUp.x),radius/Mathf.Abs(s.y)+Mathf.Abs(localUp.y),radius/Mathf.Abs(s.z)+Mathf.Abs(localUp.z));
            p.y=Mathf.Clamp(p.y,-.5f+margin.y,.5f-margin.y);
            if(bounds.shape==PortableMapBoundary.BoundaryShape.Box)
            {p.x=Mathf.Clamp(p.x,-.5f+margin.x,.5f-margin.x);p.z=Mathf.Clamp(p.z,-.5f+margin.z,.5f-margin.z);}
            else {var radial=new Vector2(p.x,p.z);radial=Vector2.ClampMagnitude(radial,Mathf.Max(0,.5f-Mathf.Max(margin.x,margin.z)));p.x=radial.x;p.z=radial.y;}
            Vector3 correction=bounds.transform.TransformPoint(p)-transform.position;
            // A second zero-distance Move clears CharacterController's grounded contact.
            if(correction.sqrMagnitude>1e-10f) body.Move(correction);
        }
        void UpdateCamera(float dt)
        {
            if(!view || mode==CameraMode.FreeCamera) return;
            var rotation=Quaternion.Euler(pitch,yaw,0);Vector3 pivot=transform.position+Vector3.up*(height*.35f);
            Vector3 arm=rotation*Vector3.back;float distance=cameraDistance;
            foreach(var hit in Physics.SphereCastAll(pivot,.08f,arm,cameraDistance,collisionMask,QueryTriggerInteraction.Ignore))
                if(!IsPreview(hit.transform)) distance=Mathf.Min(distance,Mathf.Max(.1f,hit.distance-.03f));
            view.transform.SetPositionAndRotation(pivot+arm*distance,rotation);
        }
        void CaptureCursor() {if(!ownsCursor){previousLock=Cursor.lockState;previousCursor=Cursor.visible;ownsCursor=true;}Cursor.lockState=CursorLockMode.Locked;Cursor.visible=false;}
        void ReleaseCursor() {if(ownsCursor){Cursor.lockState=previousLock;Cursor.visible=previousCursor;ownsCursor=false;}}
        void OnApplicationFocus(bool focused) {if(!focused) ReleaseCursor();}
        void OnDisable() {if(view){view.enabled=false;view.GetComponent<AudioListener>().enabled=false;}ReleaseCursor();foreach(var c in previousCameras)if(c)c.enabled=true;foreach(var a in previousListeners)if(a)a.enabled=true;previousCameras.Clear();previousListeners.Clear();}
        void OnDestroy() {if(material){if(Application.isPlaying)Destroy(material);else DestroyImmediate(material);}}
        void OnGUI()
        {
            if(!Application.isPlaying) return;
            GUI.Label(new Rect(12,12,900,28),mode==CameraMode.FreeCamera?"Free Camera · WASD / Q E · Shift: faster · F: Third Person":"Third Person · WASD · Space: jump / hold to glide · Shift: run / release climb · F: Free Camera");
            GUI.Label(new Rect(12,36,520,24),"Right mouse: capture mouse · Esc: release mouse"+(IsClimbing?" · Climbing":""));
        }
        void OnDrawGizmos() {if(!Application.isPlaying){Gizmos.color=new Color(.7f,.5f,.25f);Gizmos.DrawWireCube(transform.position,new Vector3(radius*2,height,radius*2));}}
    }
}
